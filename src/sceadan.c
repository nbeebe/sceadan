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

#include "sceadan.h"





/* LINKED INCLUDES */                                      /* LINKED INCLUDES */
#include <math.h>
/* END LINKED INCLUDES */                              /* END LINKED INCLUDES */

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MODEL ("model.ucv-bcv.20130509.c256.s2.e005")

#define RANDOMNESS_THRESHOLD (.995)
#define UCV_CONST_THRESHOLD  (.5)
#define BCV_CONST_THRESHOLD  (.5)


///////////////////////////////////////////////////////////////////////////////////////////////////
#define BZ2_BLOCK_SZ     (9)
#define BZ2_WORK_FACTOR  (0)

#define ZL_LEVEL (9)
#define WASTE_SZ (4096)
////////////////////////////////////////////////////////////////////////////////////////////////////


struct sceadan_type_t sceadan_types[] = {
    {0,"unclassified"},
    {A85,"a85"},
    {AES,"aes"},
    {ASPX,"aspx"},
    {AVI,"avi"},
    {B16,"b16"},
    {B64,"b64"},
    {BCV_CONST,"bcv_const"},
    {BMP,"bmp"},
    {BZ2,"bz2"},
    {CSS,"css"},
    {CSV,"csv"},
    {DLL,"dll"},
    {DOC,"doc"},
    {DOCX,"docx"},
    {ELF,"elf"},
    {EXE,"exe"},
    {EXT3,"ext3"},
    {FAT,"fat"},
    {FLV,"flv"},
    {GIF,"gif"},
    {GZ,"gz"},
    {HTML,"html"},
    {JAR,"jar"},
    {JAVA,"java"},
    {JB2,"jb2"},
    {JPG,"jpg"},
    {JS,"js"},
    {JSON,"json"},
    {LOG,"log"},
    {M4A,"m4a"},
    {MOV,"mov"},
    {MP3,"mp3"},
    {MP4,"mp4"},
    {NTFS,"ntfs"},
    {PDF,"pdf"},
    {PNG,"png"},
    {PPS,"pps"},
    {PPT,"ppt"},
    {PPTX,"pptx"},
    {PS,"ps"},
    {PST,"pst"},
    {RAND,"rand"},
    {RPM,"rpm"},
    {RTF,"rtf"},
    {SWF,"swf"},
    {TBIRD,"tbird"},
    {TCV_CONST,"tcv_const"},
    {TEXT,"txt"},
    {TIF,"tif"},
    {UCV_CONST,"ucv_const"},
    {URL,"url"},
    {WAV,"wav"},
    {WMA,"wma"},
    {WMV,"wmv"},
    {XLS,"xls"},
    {XLSX,"xlsx"},
    {XML,"xml"},
    {ZIP,"zip"},
    {0,""}
};


const char *sceadan_name_for_type(int code)
{
    for(int i=0;sceadan_types[i].name!=0;i++){
        if(sceadan_types[i].code==code) return sceadan_types[i].name;
    }
    return(0);
}

static size_t min ( const size_t a, const size_t b ) {
    return a < b ? a : b;
}

static sum_t max ( const sum_t a, const sum_t b ) {
    return a > b ? a : b;
}


/* FUNCTIONS FOR VECTORS */
static void vectors_update (const uint8_t      buf[],
                            const size_t           sz,
                            sceadan_vectors_t *v,
                            sum_t     *const last_cnt,
                            unigram_t *const last_val )
{
    const int sz_mod = v->mfv.uni_sz % 2;

    size_t ndx;
    for (ndx = 0; ndx < sz; ndx++) {

        /* Compute the unigrams */
        const unigram_t unigram = buf[ndx];
        v->ucv[unigram].tot++;

        {
            unigram_t prev;
            unigram_t next;

            if (ndx == 0 && sz_mod != 0) {
                prev = *last_val;
                next = unigram;

                v->bcv[prev][next].tot++;
                v->mfv.contiguity.tot += abs (next - prev);
            } else if (ndx + 1 < sz) {
                prev = unigram;
                next = buf[ndx + 1];
                v->mfv.contiguity.tot += abs (next - prev);
                if (ndx % 2 == sz_mod) v->bcv[prev][next].tot++;
            }
        }

        // total count of set bits (for hamming weight)
        // this is wierd
        v->mfv.hamming_weight.tot += (nbit_unigram - __builtin_popcount (unigram));

        // byte value sum
        v->mfv.byte_value.tot += unigram;

        // standard deviation of byte values
        v->mfv.stddev_byte_val.tot += __builtin_powi ((double) unigram, 2);

        if (*last_cnt != 0
            && *last_val == unigram)
            (*last_cnt)++;
        else {
            *last_cnt = 1;
            *last_val = unigram;
        }
        v->mfv.max_byte_streak.tot = max (*last_cnt, v->mfv.max_byte_streak.tot);

        // count of low ascii values
        if       (unigram < ASCII_LO_VAL) v->mfv.lo_ascii_freq.tot++;

        // count of medium ascii values
        else if (unigram < ASCII_HI_VAL) v->mfv.med_ascii_freq.tot++;

        // count of high ascii values
        else {
            v->mfv.hi_ascii_freq.tot++;
        }
    }

    v->mfv.uni_sz += sz;
}

static void vectors_finalize ( sceadan_vectors_t *v)
{
    // hamming weight
    v->mfv.hamming_weight.avg = (double) v->mfv.hamming_weight.tot
        / (v->mfv.uni_sz * nbit_unigram);

    // mean byte value
    v->mfv.byte_value.avg = (double) v->mfv.byte_value.tot / v->mfv.uni_sz;

    // average contiguity between bytes
    v->mfv.contiguity.avg = (double) v->mfv.contiguity.tot / v->mfv.uni_sz;

    // max byte streak
    //v->mfv.max_byte_streak = max_cnt;
    v->mfv.max_byte_streak.avg = (double) v->mfv.max_byte_streak.tot / v->mfv.uni_sz;

    // TODO skewness ?
    double expectancy_x3 = 0;
    double expectancy_x4 = 0;

    const double central_tendency = v->mfv.byte_value.avg;
    for (int i = 0; i < n_unigram; i++) {

        v->mfv.abs_dev += v->ucv[i].tot * fabs (i - central_tendency);

        // unigram frequency
        v->ucv[i].avg = (double) v->ucv[i].tot / v->mfv.uni_sz;

        // item entropy
        double pv = v->ucv[i].avg;
        if (fabs(pv)>0) // TODO floating point mumbo jumbo
            v->mfv.item_entropy += pv * log2 (1 / pv) / nbit_unigram; // more divisions for accuracy

        for (int j = 0; j < n_unigram; j++) {

            v->bcv[i][j].avg = (double) v->bcv[i][j].tot / (v->mfv.uni_sz / 2); // rounds down

            // bigram entropy
            pv = v->bcv[i][j].avg;
            if (fabs(pv)>0) // TODO
                v->mfv.bigram_entropy  += pv * log2 (1 / pv) / nbit_bigram;
        }

        const double extmp = __builtin_powi ((double) i, 3) * v->ucv[i].avg;

        // for skewness
        expectancy_x3 += extmp;

        // for kurtosis
        expectancy_x4 += extmp * i;
    }

    const double variance  = (double) v->mfv.stddev_byte_val.tot / v->mfv.uni_sz
        - __builtin_powi (v->mfv.byte_value.avg, 2);

    v->mfv.stddev_byte_val.avg = sqrt (variance);

    const double sigma3    = variance * v->mfv.stddev_byte_val.avg;
    const double variance2 = __builtin_powi (variance, 2);

    // average absolute deviation
    v->mfv.abs_dev /= v->mfv.uni_sz;
    v->mfv.abs_dev /= n_unigram;

    // skewness
    v->mfv.skewness = (expectancy_x3
                     - v->mfv.byte_value.avg * (3 * variance
	                                      + __builtin_powi (v->mfv.byte_value.avg,
	                                                        2))) / sigma3;

    // kurtosis
    if ( (isinf (expectancy_x4))) {
        fprintf (stderr, "isinf (expectancy_x4)\n");
        assert (0);
    }
    if ( (isinf (variance2))) {
        fprintf (stderr, "isinf (variance2)\n");
        assert (0);
    }

    v->mfv.kurtosis = (expectancy_x4 / variance2);
    v->mfv.byte_value.avg      /= n_unigram;
    v->mfv.stddev_byte_val.avg /= n_unigram;
    v->mfv.kurtosis            /= n_unigram;
    v->mfv.contiguity.avg      /= n_unigram;
    v->mfv.bzip2_len.avg        = 1;
    v->mfv.lzw_len.avg          = 1;
    v->mfv.lo_ascii_freq.avg  = (double) v->mfv.lo_ascii_freq.tot  / v->mfv.uni_sz;
    v->mfv.med_ascii_freq.avg = (double) v->mfv.med_ascii_freq.tot / v->mfv.uni_sz;
    v->mfv.hi_ascii_freq.avg  = (double) v->mfv.hi_ascii_freq.tot  / v->mfv.uni_sz;
}
/* END FUNCTIONS FOR VECTORS */



static int do_predict ( sceadan_vectors_t *v,
                        file_type_e  *const file_type,
                        struct feature_node *x, 
                        const struct model*      model_ )
{
    int n;
    int nr_feature=get_nr_feature(model_);
    if(model_->bias>=0){
        n=nr_feature+1;
    } else {
        n=nr_feature;
    }
    
    int i = 0;
    
    /* Add the unigrams to the vector */
    for (int k = 0 ; k < n_unigram; k++, i++) {
        x[i].index = i + 1;
        x[i].value = v->ucv[k].avg;
    }
    
    /* Add the bigrams to the vector */
    for (int k = 0; k < n_unigram; k++)
        for (int j = 0; j < n_unigram; j++) {
            x[i].index = i + 1;
            x[i].value = v->bcv[k][j].avg;
            i++;
        }
    
    /* Add the Bias */
    if(model_->bias>=0)    {
        x[i].index = n;
        x[i].value = model_->bias;
        i++;
    }
    x[i].index = -1;                    /* end of vectors? */
    
    int predict_label = predict(model_,x); /* run the predictor */
    *file_type = (file_type_e) predict_label;
    return 0;
}


static int predict_liblin (    const struct model *model_,
                               sceadan_vectors_t *v,
                               file_type_e *const file_type )
{
    if (v->mfv.item_entropy > RANDOMNESS_THRESHOLD) {
        *file_type = RAND;
        return 0;
    }
    
    for (int i = 0; i < n_unigram; i++) {
        // TODO floating point comparison
        if (v->ucv[i].avg > UCV_CONST_THRESHOLD) {
            *file_type = UCV_CONST;
            v->mfv.const_chr[0] = i;
            return 0;
        }
        for (int j = 0; j < n_unigram; j++)
            // TODO floating point comparison
            if (v->bcv[i][j].avg > BCV_CONST_THRESHOLD) {
                *file_type = BCV_CONST;
                v->mfv.const_chr[0] = i;
                v->mfv.const_chr[1] = j;
                return 0;
            }
    }
    
    const int max_nr_attr = n_bigram + n_unigram + 3;//+ /*20*/ 17 /*6 + 2 + 9*/;
    struct feature_node *x = (struct feature_node *) malloc(max_nr_attr*sizeof(struct feature_node));
    do_predict(v, file_type,x, model_);
    free(x);
    return 0;
}

/* FUNCTIONS FOR PROCESSING BLOCKS */
static int
process_blocks0 (
    const char          path[],
    const int          fd,
    const unsigned int  block_factor,
    const output_f      do_output,
    file_type_e file_type )
{
    size_t offset = 0;

    while (true) {
        sceadan_vectors_t v; memset(&v,0,sizeof(v));

        v.mfv.id_type = ID_CONTAINER;
        v.mfv.id_container = path;
        v.mfv.id_block = offset;
		
        sum_t     last_cnt = 0;
        unigram_t last_val;

        size_t tot;
        for (tot = 0; tot != block_factor; ) {
            uint8_t   buf[BUFSIZ];
            size_t rd = read (fd, buf, min (block_factor - tot, BUFSIZ));
            switch (rd) {
            case -1:
                fprintf (stderr, "fail: read ()\n");
                return 1;
            case  0:
                if (tot != 0) {
                    //	vectors_update ((unigram_t *) buf, tot, ucv, bcv, &mfv, &last_cnt, &last_val, &max_cnt);

                    vectors_finalize (&v);

                    switch (file_type) {
                    case UNCLASSIFIED:
                        if ((predict_liblin (sceadan_model_default(),&v, &file_type) != 0)) {
                            return 6;
                        }
                        //	break;
                    default:
                        if ( (do_output (&v,file_type) != 0)) {
                            return 5;
                        }
                    }
                }

                return 0; // break from while-loop
            default:

                vectors_update (buf, rd, &v, &last_cnt, &last_val/*, &max_cnt*/);

                tot += rd;
                // continue
            } // end switch
        } // end for

        vectors_finalize (&v);

        if(file_type==UNCLASSIFIED){
            if((predict_liblin (sceadan_model_default(), &v, &file_type) != 0)){
                return 6;
            }
        }
        if ((do_output (&v, file_type) != 0)) {
            return 5;
        }
        offset += tot;
    } // end while
    __builtin_unreachable ();
}

int process_blocks (    const char         path[],
                        const unsigned int block_factor,
                        const output_f     do_output,
                        file_type_e file_type )
{
    fprintf(stderr,"process_blocks %s\n",path);
    const int fd = open(path, O_RDONLY|O_BINARY);
    if ( (fd == -1)) {
        fprintf (stderr, "fail: open2 ()\n");
        return 2;
    }
    
    if (process_blocks0 (path, fd, block_factor, do_output, file_type)){
        close (fd);
        return 3;
    }

    if (close (fd) == -1) {
        perror("close");
        return 4;
    }

    return 0;
}

int process_container ( const char            path[],
                        const output_f        do_output,
                        file_type_e file_type ) 
{
    fprintf(stderr,"process_container %s\n",path);

    sceadan_vectors_t v; memset(&v,0,sizeof(v));
    v.mfv.id_type = ID_CONTAINER;// = MFV_CONTAINER_LIT;
    v.mfv.id_container = path;

    sum_t     last_cnt = 0;
    unigram_t last_val;

    const int fd = open(path, O_RDONLY|O_BINARY);
    if (fd<0){
        perror("open");
        return 2;
    }

    while (true) {
        char    buf[BUFSIZ];
        const ssize_t rd = read (fd, buf, sizeof (buf));
        if(rd==-1){
            perror("read");
            return 1;
        }
        if(rd==0) break;
        vectors_update ((unigram_t *) buf, rd, &v,&last_cnt, &last_val);
    }

    if (close (fd) == -1) {
        perror("close");
        return 4;
    }

    vectors_finalize (&v);

    if(file_type==UNCLASSIFIED){
        if ( (predict_liblin (sceadan_model_default(),&v,&file_type) != 0)) {
            return 6;
        }
    }
    if ((do_output (&v,file_type) != 0)) {
        return 5;
    }
    return 0;
}
/* END FUNCTIONS FOR PROCESSING CONTAINERS */


// prints finalized vectors
int output_competition (struct sceadan_vectors *v,
                        file_type_e file_type )
{
    assert((v->mfv.id_type==ID_CONTAINER) ||  (v->mfv.id_type==ID_BLOCK));
    printf ("%-10zu %s # %s\n", v->mfv.id_block,sceadan_name_for_type(file_type),v->mfv.id_container);
    return 0;
}

struct model *model_ = 0;
const struct model *sceadan_model_default()
{
    if(model_==0){
        model_=load_model(MODEL);
        if(model_==0){
            fprintf(stderr,"can't open model file %s\n","");
            return 0;
        }
    }
    return model_;
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
    for(int i=0;i<model->nr_feature;i++){
        printf("%g",model->w[i]);
        if(i<model->nr_feature-1) putchar(',');
        if(i%10==9) printf("\n\t");
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


sceadan *sceadan_open(const char *model_name) // use 0 for default model
{
    sceadan *s = (sceadan *)calloc(sizeof(sceadan *),1);
    if(model_name){
        s->model = load_model(model_name);
        if(s->model==0){
            free(s);
            return 0;
        }
        return s;
    }
    s->model = sceadan_model_default();
    return s;
}

int sceadan_classify_buf(const sceadan *s,const uint8_t *buf,size_t bufsize)
{
    sceadan_vectors_t v; memset(&v,0,sizeof(v));
    v.mfv.id_type = ID_CONTAINER;// = MFV_CONTAINER_LIT;
    sum_t     last_cnt = 0;
    unigram_t last_val;

    vectors_update (buf, bufsize, &v, &last_cnt, &last_val);
    vectors_finalize (&v);
    file_type_e file_type=0;
    predict_liblin (s->model,&v, &file_type);
    return file_type;
}

int sceadan_classify_file(const sceadan *s,const char *fname)
{
    struct sceadan_vectors v; memset(&v,0,sizeof(v));
    v.mfv.id_type = ID_CONTAINER;// = MFV_CONTAINER_LIT;
    sum_t     last_cnt = 0;
    unigram_t last_val;

    const int fd = open(fname, O_RDONLY|O_BINARY);
    if (fd<0) return -1;                /* error condition */
    while (true) {
        uint8_t    buf[BUFSIZ];
        const ssize_t rd = read (fd, buf, sizeof (buf));
        if(rd<=0) break;
        vectors_update (buf, rd, &v, &last_cnt, &last_val);
    }
    if(close(fd)<0) return -1;
    vectors_finalize (&v);
    file_type_e file_type=UNCLASSIFIED;
    predict_liblin (s->model,&v, &file_type);
    return file_type;
}

