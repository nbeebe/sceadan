/*****************************************************************
 *
 *
 *Copyright (c) 2012-2013 The University of Texas at San Antonio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright Holder:
 * University of Texas at San Antonio
 * One UTSA Circle 
 * San Antonio, Texas 78209
 */

/*
 *
 * Development History:
 *
 * 2014 - Significant refactoring and updating by:
 * Simson L. Garfinkel, Naval Postgraduate School
 *
 * 2013 - Created by:
 * Dr. Nicole Beebe and Lishu Liu, Department of Information Systems and Cyber Security (nicole.beebe@utsa.edu)
 * Laurence Maddox, Department of Computer Science
 * 
 */

#include "config.h"
#include "sceadan.h"

#include <assert.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

/* We require liblinear */
#ifdef HAVE_LIBLINEAR

#ifdef HAVE_LINEAR_H
#include <linear.h>
#endif
#ifdef HAVE_LIBLINEAR_LINEAR_H
#include <liblinear/linear.h>
#endif

/* definitions. Some will be moved out of this file */

typedef uint8_t  unigram_t;  /* unigram  */
typedef uint16_t bigram_t;   /* bigram  two consecutive unigrams; (first<<8)|second */
uint32_t const nbit_unigram=8;                // bits in a unigram
uint32_t const nbit_bigram=16;               /* number of bits in a bigram */

#define NUNIGRAMS 256   /* number of possible unigrams   = 2 ** 8 (needs at least 9 bits) */
#define NBIGRAMS 65536  /* number of possible bigrams = 2 ** 16 (needs at least 17 bits) */
#define bigramcode(f,s) ((f<<8)+s)


/* Liblinear index mapping: */
static const int START_UNIGRAMS=1;  /* 1..256 - unigram counts */
static const int START_BIGRAMS_ALL=START_UNIGRAMS+NUNIGRAMS; /* 257+FS - all bigram counts for bigram FS (characters F and S, where FS=F<<8|S) */
static const int START_BIGRAMS_EVEN = START_BIGRAMS_ALL  + NBIGRAMS; /*  257+65536+FS - even bigram counts for bigram FS */
static const int START_BIGRAMS_ODD  = START_BIGRAMS_EVEN + NBIGRAMS; /* 257+65536*2+S - odd bigram counts for bigram FS */
static const int START_STATS        = START_BIGRAMS_ODD  + NBIGRAMS;
static const int STATS_IDX_BIGRAM_ENTROPY              = START_STATS + 0;
static const int STATS_IDX_ITEM_ENTROPY                = START_STATS + 1;
static const int STATS_IDX_HAMMING_WEIGHT              = START_STATS + 2;
static const int STATS_IDX_MEAN_BYTE_VALUE             = START_STATS + 3;
static const int STATS_IDX_STDDEV_BYTE_VAL             = START_STATS + 4;
static const int STATS_IDX_ABS_DEV                     = START_STATS + 5;
static const int STATS_IDX_SKEWNESS                    = START_STATS + 6;
static const int STATS_IDX_KURTOSIS                    = START_STATS + 7;
static const int STATS_IDX_CONTIGUITY                  = START_STATS + 8;
static const int STATS_IDX_MAX_BYTE_STREAK             = START_STATS + 9;
static const int STATS_IDX_LO_ASCII_FREQ               = START_STATS + 10;
static const int STATS_IDX_MED_ASCII_FREQ              = START_STATS + 11;
static const int STATS_IDX_HI_ASCII_FREQ               = START_STATS + 12;
static const int STATS_IDX_BYTE_VAL_CORRELATION        = START_STATS + 13;
static const int STATS_IDX_BYTE_VAL_FREQ_CORRELATION   = START_STATS + 14;
static const int STATS_IDX_UNI_CHI_SQ                  = START_STATS + 15;
static const int MAX_NR_ATTR        = START_STATS+16;

/* Tunable parameters */

const uint32_t ASCII_LO_VAL=0x20;   /* low  ascii range is  0x00 <= char < ASCII_LOW_VALUE0 */
const uint32_t ASCII_HI_VAL=0x80;   /* high ascii range is   ASCII_HI_VAL <= char */

/* type for summation/counting followed by floating point ops.
 * The original programmer had this as a union, which is just asking for trouble.
 */
typedef struct {
    uint64_t  tot;
    double    avg;
} cv_e;

/* unigram count vector map unigram to count, then to frequency
   implements the probability distribution function for unigrams TODO
   beware of overflow more implementations */
typedef cv_e ucv_t[NUNIGRAMS];

/* bigram count vector map bigram to count, then to frequency
   implements the probability distribution function for bigrams TODO
   beware of overflow more implementations */
typedef cv_e bcv_t[NUNIGRAMS][NUNIGRAMS];

/* main feature vector */
typedef struct {
    /* size of item */
    uint64_t  unigram_count;                      /* number of unigrams */

    /*  Feature Name: Bi-gram Entropy
        Description : Shannon's entropy
        Formula     : Sum of -P (v) lb (P (v)),
        over all bigram values (v)
        where P (v) = frequency of bigram value (v) */
    double bigram_entropy;

    /* Feature Name: Item Identifier
       Description : Shannon's entropy
       Formula     : Sum of -P (v) lb (P (v)),
       over all byte values (v)
       where P (v) = frequency of byte value (v) */
    double item_entropy;

    /* Feature Name: Hamming Weight
       Description : Total number of 1s divided by total bits in item */
    cv_e hamming_weight;

    /* Feature Name: Mean Byte Value
       Description : Sum of all byte values in item divided by total size in bytes
       Note        : Only go to EOF if item is an allocated file */
    cv_e mean_byte_value;

    /* Feature Name: Standard Deviation of Byte Values
       Description : Typical standard deviation formula */
    cv_e stddev_byte_val;

    /* (from wikipedia:)
       the (average) absolute deviation of an element of a data set is the
       absolute difference between that element and ... a measure of central
       tendency
       TODO use median instead of mean */
    double abs_dev;

    /* SKEWNESS is a measure of the asymmetry of the probability distribution of
       a real-valued random variable */
    double skewness;

    /* KURTOSIS  Shows peakedness in the byte value distribution graph */
    double kurtosis;

    /* Feature Name: Average contiguity between bytes
       Description : Average distance between consecutive byte values */
    cv_e  contiguity;

    /* Feature Name: Max Byte Streak
       Description : Length of longest streak of repeating bytes in item
       TODO normalize ? */
    cv_e  max_byte_streak;

    /* Feature Name: Low ASCII frequency
       Description : Frequency of bytes 0x00 .. 0x1F,
       normalized by item size */
    cv_e lo_ascii_freq;

    /* Feature Name: ASCII frequency
       Description : Frequency of bytes 0x20 .. 0x7F,
       normalized by item size */
    cv_e med_ascii_freq;

    /* Feature Name: High ASCII frequency
       Description : Frequency of bytes 0x80 .. 0xFF,
       normalized by item size */
    cv_e hi_ascii_freq;

    /* Feature Name: Byte Value Correlation
       Description : Correlation of the values of the n and n+1 bytes */
    double byte_val_correlation;

    /* Feature Name: Byte Value Frequency Correlation
       Description : Correlation of the frequencies of values of the m and m+1 bytes */
    double byte_val_freq_correlation;

    /* Feature Name: Unigram chi square
       Description : Test goodness-of-fit of unigram count vector relative to
       random distribution of byte values to see if distribution
       varies statistically significantly from a random
       distribution */
    double uni_chi_sq;
} mfv_t;

/* END TYPEDEFS FOR I/O */
struct sceadan_type_t {
    int code;
    const char *name;
};

extern struct sceadan_type_t sceadan_types[];

/* FUNCTIONS */
// TODO full path vs relevant path may matter
struct sceadan_vectors {
    ucv_t ucv;                          /* unigram statistics */
    bcv_t bcv_all;                      /* all bigram statistics */
    bcv_t bcv_even;                     /* even bigram statistics */
    bcv_t bcv_odd;                      /* odd bigram statistics */
    mfv_t mfv;                          /* other statistics; # of unigrams processes is mfv.unigram_count */
    uint8_t prev_value;                   /* last value from previous loop iteration */
    uint64_t prev_count;                     /* number of prev_vals in a row*/
    const char *file_name;              /* if the vectors came from a file, indicate it here */
};
typedef struct sceadan_vectors sceadan_vectors_t;

#define MODEL_DEFAULT_FILENAME ("model")                 /* default model file */

#define RANDOMNESS_THRESHOLD (.995)     /* ignore things more random than this */
#define UCV_CONST_THRESHOLD  (.5)       /* ignore UCV more than this */
#define BCV_CONST_THRESHOLD  (.5)       /* ignore BCV more than this */

static inline double square(double v) { return v*v;}
static inline double cube(double v)   { return v*v*v;}

const char *sceadan_name_for_type(const sceadan *s,int code)
{
    /* This may seem odd, but we don't know how long the array is */
    for(int i=0;s->types[i];i++){
        if(i==code) return s->types[i];
    }
    return(0);
}

int sceadan_type_for_name(const sceadan *s,const char *name)
{
    for(int i=0;s->types[i];i++){
        if(strcasecmp(name,s->types[i])==0) return i;
    }
    return(-1);
}

static uint64_t max ( const uint64_t a, const uint64_t b ) {
    return a > b ? a : b;
}


/****************************************************************
 *** liblinear node/vector interface
 ****************************************************************/

/** Create the sparse liblinear fature_node structure from the vectors used by sceadan. **/

#define assert_and_set(i) {assert(set[i]==0);set[i]=1;}
#define set_index_value(k,v) {assert(idx<MAX_NR_ATTR);x[idx].index = k; x[idx].value = v; idx++;}
static void build_nodes_from_vectors(const sceadan *s, const sceadan_vectors_t *v, struct feature_node *x )
{
    int idx = 0;                        /* cannot exceed MAX_NR_ATTR */
    int set[MAX_NR_ATTR];
    memset(set,0,sizeof(set));
    
    /* Add the unigrams to the vector */
    for (int i = 0 ; i < NUNIGRAMS; i++) {
        if(v->ucv[i].avg > 0.0){
            set_index_value(START_UNIGRAMS + i,v->ucv[i].avg)
        }
    }
    
    /* Add the bigrams to the vector */
    if (s->ngram_mode & 1) {
        for (int i = 0; i < NUNIGRAMS; i++) {
            for (int j = 0; j < NUNIGRAMS; j++) {
                if (v->bcv_all[i][j].avg > 0.0) {
                    set_index_value(START_BIGRAMS_ALL+bigramcode(i,j), v->bcv_all[i][j].avg);
                }
            }
        }
    }
    if (s->ngram_mode & 2) {
        for (int i = 0; i < NUNIGRAMS; i++) {
            for (int j = 0; j < NUNIGRAMS; j++) {
                if (v->bcv_even[i][j].avg > 0.0) {
                    set_index_value(START_BIGRAMS_EVEN+bigramcode(i,j), v->bcv_even[i][j].avg);
                }
            }
        }
    }
    if (s->ngram_mode & 4) {
        for (int i = 0; i < NUNIGRAMS; i++) {
            for (int j = 0; j < NUNIGRAMS; j++) {
                if (v->bcv_odd[i][j].avg > 0.0) {
                    set_index_value(START_BIGRAMS_ODD+bigramcode(i,j), v->bcv_odd[i][j].avg);
                }
            }
        }
    }
    
    if (s->ngram_mode & 0x00008) { set_index_value(STATS_IDX_BIGRAM_ENTROPY,  v->mfv.bigram_entropy); }
    if (s->ngram_mode & 0x00010) { set_index_value(STATS_IDX_ITEM_ENTROPY,    v->mfv.item_entropy); }
    if (s->ngram_mode & 0x00020) { set_index_value(STATS_IDX_HAMMING_WEIGHT,  v->mfv.hamming_weight.avg); }
    if (s->ngram_mode & 0x00040) { set_index_value(STATS_IDX_MEAN_BYTE_VALUE, v->mfv.mean_byte_value.avg); }
    if (s->ngram_mode & 0x00080) { set_index_value(STATS_IDX_STDDEV_BYTE_VAL, v->mfv.stddev_byte_val.avg); }
    if (s->ngram_mode & 0x00100) { set_index_value(STATS_IDX_ABS_DEV,         v->mfv.abs_dev); }
    if (s->ngram_mode & 0x00200) { set_index_value(STATS_IDX_SKEWNESS,        v->mfv.skewness); }
    if (s->ngram_mode & 0x00400) { set_index_value(STATS_IDX_KURTOSIS,        v->mfv.kurtosis); }
    if (s->ngram_mode & 0x00800) { set_index_value(STATS_IDX_CONTIGUITY,      v->mfv.max_byte_streak.avg); }
    if (s->ngram_mode & 0x01000) { set_index_value(STATS_IDX_MAX_BYTE_STREAK, v->mfv.max_byte_streak.tot); /* don't normalize! */ }
    if (s->ngram_mode & 0x02000) { set_index_value(STATS_IDX_LO_ASCII_FREQ,   v->mfv.lo_ascii_freq.avg); }
    if (s->ngram_mode & 0x04000) { set_index_value(STATS_IDX_MED_ASCII_FREQ,   v->mfv.med_ascii_freq.avg); }
    if (s->ngram_mode & 0x08000) { set_index_value(STATS_IDX_HI_ASCII_FREQ,   v->mfv.hi_ascii_freq.avg); }
    if (s->ngram_mode & 0x10000) { set_index_value(STATS_IDX_BYTE_VAL_CORRELATION, v->mfv.byte_val_correlation); }
    if (s->ngram_mode & 0x20000) { set_index_value(STATS_IDX_BYTE_VAL_FREQ_CORRELATION, v->mfv.byte_val_freq_correlation); }
    if (s->ngram_mode & 0x40000) { set_index_value(STATS_IDX_UNI_CHI_SQ,      v->mfv.uni_chi_sq); }
    

    /* Add the Bias if we are using Bias. It goes last, apparently */
    if (s->model->bias >= 0 ) {
        set_index_value(get_nr_feature( s->model ) + 1, s->model->bias);
    }
    /* And note that we are at the end of the vectors */
    assert (idx < MAX_NR_ATTR) ;
    x[ idx++ ].index = -1; /* end of vectors */
}


    static void dump_vectors_as_json(const sceadan *s,const sceadan_vectors_t *v)
{
    printf("{ \"file_type\": %d,\n",s->file_type);
    if(v->file_name) printf("  \"file_name\": \"%s\",\n",v->file_name);
    printf("  \"unigrams\": { \n");
    int first = 1;
    for(int i=0;i<NUNIGRAMS;i++){
        if(v->ucv[i].avg>0){
            if(first) {
                first = 0;
            } else {
                printf(",\n");
            }
            printf("    \"%d\" : %.16lg",i,v->ucv[i].avg);
        }
    }
    printf("  },\n");
    printf("  \"bigrams:\": { \n");
    first = 1;
    for(int i=0;i<NUNIGRAMS;i++){
        for(int j=0;j<NUNIGRAMS;j++){
            if(v->bcv_all[i][j].avg>0){
                if(first){
                    first = 0;
                } else {
                    printf(",\n");
                    first = 0;
                }
                printf("    \"%d\" : %.16lg",i<<8|j,v->bcv_all[i][j].avg);
            }
        }
    }
    printf("  }\n");
#define OUTPUT(XXX) printf("  \"%s\": %.16lg,\n",#XXX,v->mfv.XXX)
    OUTPUT(bigram_entropy);
    OUTPUT(item_entropy);
    OUTPUT(hamming_weight.avg);
    OUTPUT(mean_byte_value.avg);
    OUTPUT(stddev_byte_val.avg);
    OUTPUT(abs_dev);
    OUTPUT(skewness);
    OUTPUT(kurtosis);
    OUTPUT(contiguity.avg);
    OUTPUT( max_byte_streak.avg);
    OUTPUT(lo_ascii_freq.avg);
    OUTPUT(med_ascii_freq.avg);
    OUTPUT(hi_ascii_freq.avg);
    OUTPUT(byte_val_correlation);
    OUTPUT(byte_val_freq_correlation);
    OUTPUT(uni_chi_sq);
    printf("  \"version\":1.0\n");
    printf("}\n");
}

static void dump_nodes(FILE *out,const sceadan *s,const struct feature_node *x)
{
    fprintf(out,"%d ",s->file_type);
    for(int i=0;i<MAX_NR_ATTR;i++){
        if (x[i].index && x[i].value>0) fprintf(out,"%d:%g ",x[i].index,x[i].value);
        if (x[i].index == -1) break;
    }
    fputc('\n',out);
}

/****************************************************************
 *** VECTOR GENERATION FUNCTIONS
 ****************************************************************/

static void vectors_update (const sceadan *s,const uint8_t buf[], const size_t sz, sceadan_vectors_t *v)
{
    for (size_t ndx = 0; ndx < sz; ndx++) { /* ndx is index within the buffer */

        /* First update single-byte statistics */

        const unigram_t unigram = buf[ndx];
        v->ucv[unigram].tot++;          /* unigram counter */

        /* Histogram for ASCII value ranges */
        if (unigram < ASCII_LO_VAL) v->mfv.lo_ascii_freq.tot++;
        else if (unigram < ASCII_HI_VAL) v->mfv.med_ascii_freq.tot++;
        else v->mfv.hi_ascii_freq.tot++;

        // total count of set bits (for hamming weight)
        v->mfv.hamming_weight.tot += (nbit_unigram - __builtin_popcount (unigram));
        v->mfv.mean_byte_value.tot += unigram;              /* sum of byte values */
        v->mfv.stddev_byte_val.tot += unigram*unigram; /* sum of squares */

        /* Compute the bigram values if this is not the first character seen */
        if (v->mfv.unigram_count>0){                 /* only process bigrams on characters >=1 */
            int parity = v->mfv.unigram_count % 2;

            if (s->ngram_mode & 1) v->bcv_all[v->prev_value][unigram].tot++;
            if (parity == 0) {
                if (s->ngram_mode & 2) v->bcv_even[v->prev_value][unigram].tot++;
            } else {
                if (s->ngram_mode & 4) v->bcv_odd[v->prev_value][unigram].tot++;
            }

            v->mfv.contiguity.tot += abs (unigram - v->prev_value);
            if (v->prev_value==unigram) {
                v->prev_count++;
                v->mfv.max_byte_streak.tot = max(v->prev_count, v->mfv.max_byte_streak.tot);
            } 
        }
        /* If this is the first, or if this is not the next in a streak, reset the counters */
        if (v->mfv.unigram_count==0 || v->prev_value!=unigram){
            v->prev_value = unigram;
            v->prev_count = 1;
        }

        v->mfv.unigram_count ++;
    }
}

static void vectors_finalize ( sceadan_vectors_t *v)
{
    // hamming weight
    v->mfv.hamming_weight.avg = (double) v->mfv.hamming_weight.tot / (v->mfv.unigram_count * nbit_unigram);

    // mean byte value
    v->mfv.mean_byte_value.avg = (double) v->mfv.mean_byte_value.tot / v->mfv.unigram_count;

    // average contiguity between bytes
    v->mfv.contiguity.avg = (double) v->mfv.contiguity.tot / v->mfv.unigram_count;

    // max byte streak
    //v->mfv.max_byte_streak = max_cnt;
    v->mfv.max_byte_streak.avg = (double) v->mfv.max_byte_streak.tot / v->mfv.unigram_count;

    // TODO skewness ?
    double expectancy_x3 = 0;
    double expectancy_x4 = 0;

    const double central_tendency = v->mfv.mean_byte_value.avg;
    for (int i = 0; i < NUNIGRAMS; i++) {

        // unigram frequency
        v->ucv[i].avg = (double) v->ucv[i].tot / v->mfv.unigram_count;

        v->mfv.abs_dev += v->ucv[i].tot * fabs (i - central_tendency);

        // item entropy
        // Currently calculated but not used 
        double pv = v->ucv[i].avg;
        if (fabs(pv)>0) {
            v->mfv.item_entropy += pv * log2 (1 / pv) / nbit_unigram; // more divisions for accuracy
        } 
            
        // Normalize the bigram counts
        for (int j = 0; j < NUNIGRAMS; j++) {
            v->bcv_all[i][j].avg  = (double) v->bcv_all[i][j].tot / (v->mfv.unigram_count / 2); // rounds down
            v->bcv_even[i][j].avg = (double) v->bcv_even[i][j].tot / (v->mfv.unigram_count / 4); // rounds down
            v->bcv_odd[i][j].avg = (double) v->bcv_odd[i][j].tot / (v->mfv.unigram_count / 4); // rounds down

            // bigram entropy
            // Currently calculated but not used 
            pv = v->bcv_all[i][j].avg;
            if (fabs(pv)>0) {
                v->mfv.bigram_entropy  += pv * log2 (1 / pv) / nbit_bigram;
            }
        }

        const double extmp = cube(i) * v->ucv[i].avg; 

        expectancy_x3 += extmp;        // for skewness
        expectancy_x4 += extmp * i;     // for kurtosis
    }

    const double variance  = (double) v->mfv.stddev_byte_val.tot / v->mfv.unigram_count - square(v->mfv.mean_byte_value.avg);

    v->mfv.stddev_byte_val.avg = sqrt (variance);

    const double sigma3    = variance * v->mfv.stddev_byte_val.avg;
    const double variance2 = square(variance);

    // average absolute deviation
    v->mfv.abs_dev /= v->mfv.unigram_count;
    v->mfv.abs_dev /= NUNIGRAMS;

    // skewness
    v->mfv.skewness = (expectancy_x3 - v->mfv.mean_byte_value.avg * (3 * variance + square (v->mfv.mean_byte_value.avg))) / sigma3;

    // kurtosis
    assert(isinf(expectancy_x4)==0);
    assert(isinf(variance2)==0);

    v->mfv.kurtosis = (expectancy_x4 / variance2);
    v->mfv.mean_byte_value.avg      /= NUNIGRAMS;
    v->mfv.stddev_byte_val.avg /= NUNIGRAMS;
    v->mfv.kurtosis            /= NUNIGRAMS;
    v->mfv.contiguity.avg      /= NUNIGRAMS;
    v->mfv.lo_ascii_freq.avg  = (double) v->mfv.lo_ascii_freq.tot  / v->mfv.unigram_count;
    v->mfv.med_ascii_freq.avg = (double) v->mfv.med_ascii_freq.tot / v->mfv.unigram_count;
    v->mfv.hi_ascii_freq.avg  = (double) v->mfv.hi_ascii_freq.tot  / v->mfv.unigram_count;
}

/* predict the vectors with a model and return the predicted type.
 * 
 * That is to handle vectors of too little or too much
 * variance/entropy, e.g. a file of all 0x00s will be considered
 * CONSTANT, or a file of all random unigrams will be considered
 * RANDOM. We consider those vectors abnormal and taken special care
 * of, instead of predicting. 
 */
static int sceadan_predict(const sceadan *s,const sceadan_vectors_t *v)
{
    int ret = 0;

    vectors_finalize(s->v);
    if(s->dump_json){                        /* dumping, not predicting */
        dump_vectors_as_json(s,v);
        return 0;
    }

    struct feature_node *x = (struct feature_node *) calloc(MAX_NR_ATTR,sizeof(struct feature_node));
    build_nodes_from_vectors(s,v, x);
    
    if(s->dump_nodes){
        dump_nodes(s->dump_nodes,s,x);
    } else {
        ret = predict(s->model,x);           /* run the liblinear predictor */
    }
    free(x);
    return ret;
}


const struct model *sceadan_model_default()
{
    static struct model *default_model = 0;               /* this assures that the model will only be loaded once */
    if(default_model==0){
        default_model=load_model(MODEL_DEFAULT_FILENAME);
        if(default_model==0){
            fprintf(stderr,"can't open model file %s\n","");
            return 0;
        }
    }
    return default_model;
}

void sceadan_model_dump(const struct model *model)
{
    puts("#include \"config.h\"");
    puts("#ifdef HAVE_LINEAR_H");
    puts("#include <linear.h>");
    puts("#endif");
    puts("#ifdef HAVE_LIBLINEAR_LINEAR_H");
    puts("#include <liblinear/linear.h>");
    puts("#endif");
    puts("#include \"sceadan.h\"");

    if(model->param.nr_weight){
        printf("static int weight_label[]={");
        for(int i=0;i<model->param.nr_weight;i++){
            if(i>0) putchar(',');
            printf("%d",model->param.weight_label[i]);
            if(i%10==9) printf("\n\t");
        }
        printf("};\n\n");
    }
    if(model->param.nr_weight){
        printf("static double weight[] = {");
        for(int i=0;i<model->param.nr_weight;i++){
            if(i>0) putchar(',');
            printf("%g",model->param.weight[i]);
            if(i%10==9) printf("\n\t");
        }
        printf("};\n\n");
    }

    printf("static int label[] = {");
    for(int i=0;i<model->nr_class;i++){
        printf("%d",model->label[i]);
        if(i<model->nr_class-1) putchar(',');
        if(i%20==19) printf("\n\t");
    }
    printf("};\n");

    printf("static double w[] = {");
    int n;
    if(model->bias>=0){
        n = model->nr_feature+1;
    } else {
        n = model->nr_feature;
    }
    int w_size = n;
    int nr_w;
    if(model->nr_class==2 && model->param.solver_type != 4){
        nr_w = 1;
    } else {
        nr_w = model->nr_class;
    }

    for(int i=0;i<w_size;i++){
        for(int j=0;j<nr_w;j++){
            printf("%.16lg",model->w[i*nr_w+j]);
            if(i!=w_size-1 || j!=nr_w-1) putchar(',');
            if(j%10==9) printf("\n\t");
        }
        printf("\n\t");
    }
    printf("};\n");
        
    printf("static struct model m = {\n");
    printf("\t.param = {\n");
    printf("\t\t.solver_type=%d,\n",model->param.solver_type);
    printf("\t\t.eps = %g,\n",model->param.eps);
    printf("\t\t.C = %g,\n",model->param.C);
    printf("\t\t.nr_weight = %d,\n",model->param.nr_weight);
    printf("\t\t.weight_label = %s,\n",model->param.nr_weight ? "weight_label" : "0");
    printf("\t\t.weight = %s,\n",model->param.nr_weight ? "weight" : "0");
    printf("\t\t.p = %g},\n",model->param.p);

    printf("\t.nr_class=%d,\n",model->nr_class);
    printf("\t.nr_feature=%d,\n",model->nr_feature);
    printf("\t.w=w,\n");
    printf("\t.label=label,\n");
    printf("\t.bias=%g};\n",model->bias);

    printf("const struct model *sceadan_model_precompiled(){return &m;}\n");
}


static const char *sceadan_map_precompiled[] =
{"UNCLASSIFIED", "TEXT", "CSV", "LOG", "HTML", "XML", "ASPX", "JSON", "JS", "JAVA", 
 "CSS", "B64", "B85", "B16", "URL", "PS", "RTF", "TBIRD", "PST", "PNG",
 "GIF", "TIF", "JB2", "GZ", "ZIP", "JAR", "RPM", "BZ2", "PDF", "DOCX", 
 "XLSX", "PPTX", "JPG", "MP3", "M4A", "MP4", "AVI", "WMV", "FLV", "SWF", 
 "WAV", "WMA", "MOV", "DOC",  "XLS", "PPT", "FS-FAT", "FS-NTFS", "FS-EXT", "EXE",
 "DLL", "ELF", "BMP", "AES", "RAND",  "PPS",
 0};

#else
#warn Sceadan requires LIBLINEAR
#endif  /* HAVE_LIBLINEAR */


/*
 * Open another classifier, reading both a model and a map.
 */
sceadan *sceadan_open(const char *model_file,const char *map_file) // use 0 for default model
{
#ifdef HAVE_LIBLINEAR
    sceadan *s = (sceadan *)calloc(sizeof(sceadan),1);
    if(model_file){
        s->model = load_model(model_file);
        if(s->model==0){
            free(s);
            return 0;
        }
    } else {
        s->model = sceadan_model_precompiled();
    }
    s->v = (sceadan_vectors_t *)calloc(sizeof(sceadan_vectors_t),1);
    if(map_file){
        assert(0);                      /* need to write this code */
    } else {
        s->types = sceadan_map_precompiled;
    }
    return s;
#else
    return 0;                           /* no liblinear */
#endif
}

void sceadan_close(sceadan *s)
{
#ifdef HAVE_LIBLINEAR
    free(s->v);
    memset(s,0,sizeof(*s));             /* clean object re-use */
    free(s);
#endif
}

void sceadan_clear(sceadan *s)
{
#ifdef HAVE_LIBLINEAR
    memset(s->v,0,sizeof(sceadan_vectors_t));
#endif
}

int sceadan_classify_buf(const sceadan *s,const uint8_t *buf,size_t bufsize)
{
#ifdef HAVE_LIBLINEAR
    sceadan_vectors_t v;
    memset(&v,0,sizeof(v));
    vectors_update(s,buf, bufsize, &v);
    return sceadan_predict(s,&v);
#else
    return -1;
#endif
}

void sceadan_update(sceadan *s,const uint8_t *buf,size_t bufsize)
{
#ifdef HAVE_LIBLINEAR
    vectors_update(s,buf, bufsize, s->v);
#endif
}

int sceadan_classify(sceadan *s)
{
#ifdef HAVE_LIBLINEAR
    int r = sceadan_predict(s,s->v);
    sceadan_clear(s);
    return r;
#else
    return -1;
#endif
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

int sceadan_classify_file(const sceadan *s,const char *file_name)
{
#ifdef HAVE_LIBLINEAR
    sceadan_vectors_t v;
    memset(&v,0,sizeof(v));
    v.file_name = file_name;
    const int fd = open(file_name, O_RDONLY|O_BINARY);
    if (fd<0) return -1;                /* error condition */
    while (true) {
        uint8_t    buf[BUFSIZ];
        const ssize_t rd = read (fd, buf, sizeof (buf));
        if(rd<=0) break;
        vectors_update(s,buf,rd,&v);
    }
    if(close(fd)<0) return -1;
    return sceadan_predict(s,&v);
#else
    return -1;
#endif
}

void sceadan_dump_json_on_classify(sceadan *s,int file_type,FILE *out)
{
#ifdef HAVE_LIBLINEAR
    s->dump_json = out;
    s->file_type = file_type;
#endif
}

void sceadan_dump_nodes_on_classify(sceadan *s,int file_type,FILE *out)
{
#ifdef HAVE_LIBLINEAR
    s->dump_nodes = out;
    s->file_type = file_type;
#endif
}

void sceadan_set_ngram_mode(sceadan *s,int ngram_mode)
{
    s->ngram_mode = ngram_mode;
}