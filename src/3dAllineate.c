/**** TO DO (someday, maybe):
        -matini
****/

/****** N.B.: What used to be 'target' is now 'source' to users,
              but remains as 'target' in the code and comments.  ******/

/****** N.B.: See the note about coordinates at the end of this file! ******/

/*
  [PT: Nov 5, 2018] Don't know why this is only happening *now*, but
  am moving the lpa/lpc cost functions into the main list under '-cost
  ccc' in the help; they are no longer classified as "experimental"
 */

/*----------------------------------------------------------------------------*/
#include "mrilib.h"
#include "r_new_resam_dset.h"
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef USE_OMP
#include <omp.h>
# if 0
#  ifdef USING_MCW_MALLOC
#   include "mcw_malloc.c"
#  endif
#endif
#endif

#define MAXPAR   999
#define PARC_FIX 1
#define PARC_INI 2
#define PARC_RAN 3
typedef struct { int np,code; float vb,vt ; } param_opt ;

#define WARP_SHIFT    1
#define WARP_ROTATE   2
#define WARP_SCALE    3
#define WARP_AFFINE   4

#define APPLY_PARAM   1   /* 23 Jul 2007 */
#define APPLY_AFF12   2

/*** parameter counts for the obsolescent -nwarp option ***/

#define NPBIL          39 /* plus 4 */
#define WARP_BILINEAR 661
#define APPLY_BILIN     3

#define NPCUB          60 /* 3*(1+3+6+10) */
#define WARP_CUBIC    663
#define APPLY_CUBIC     4

#define NPQUINT       168 /* 3*(1+3+6+10+15+21) */
#define WARP_QUINT    664
#define APPLY_QUINT     5

#define NPHEPT        360 /* 3*(1+3+6+10+15+21+28+36) */
#define WARP_HEPT     665
#define APPLY_HEPT      6

#define NPNONI        660 /* 3*(1+3+6+10+15+21+28+36+45+55) */
#define WARP_NONI     666
#define APPLY_NONI      7

#define NONLINEAR_IS_POLY(aa) ( (aa) >= WARP_CUBIC && (aa) <= WARP_NONI )

#define NONLINEAR_APPLY(aa) ( (aa) >= APPLY_BILIN )

/****/

#define DEFAULT_TBEST     5
#define DEFAULT_MICHO_MI  0.2
#define DEFAULT_MICHO_NMI 0.2
#define DEFAULT_MICHO_CRA 0.4
#define DEFAULT_MICHO_HEL 0.4
#define DEFAULT_MICHO_OV  0.4

/****/

static float wt_medsmooth = 2.25f ;   /* for mri_weightize() */
static float wt_gausmooth = 4.50f ;
MRI_IMAGE * mri_weightize( MRI_IMAGE *, int, int, float,float ); /* prototype */

static int  doing_2D = 0 ; /* 28 Apr 2020: 2D registration */
static int  verb     = 1 ; /* somewhat on by default */

/* for checking how far 2 sets of parameters are apart from each other */

float param_dist( GA_setup *stp , float *aa , float *bb ) ;      /* prototype */

void AL_setup_warp_coords( int,int,int,int ,
                           int *, float *, mat44,
                           int *, float *, mat44 ) ;             /* prototype */

MRI_IMAGE * mri_identity_params(void);                           /* prototype */

#undef MEMORY_CHECK
#ifdef USING_MCW_MALLOC
# define MEMORY_CHECK(mm)                                               \
   do{ if( verb > 5 ) mcw_malloc_dump() ;                               \
       if( verb > 1 ){                                                  \
         long long nb = mcw_malloc_total() ;                            \
         if( nb > 0 ) INFO_message("Memory usage now = %s (%s): %s" ,   \
                      commaized_integer_string(nb) ,                    \
                      approximate_number_string((double)nb) , (mm) ) ;  \
       }                                                                \
   } while(0)
#else
# define MEMORY_CHECK(mm) /*nada*/
#endif

#define ALLOW_METH_CHECK   /* for the -check option: 03 Apr 2008 */

/*----------------------------------------------------------------------------*/
/*** Stuff that defines the method codes and name ***/

#undef  NMETH
#define NMETH GA_MATCH_METHNUM_SCALAR  /* cf. mrilib.h */

static int meth_visible[NMETH] =       /* 1 = show in -help; 0 = don't show */
  { 1 , 0 , 1 , 1 , 1 , 0 , 1 , 1 , 1 , 0 , 1 , 1 , 1  , 1 } ;
/* ls  sp  mi  crM nmi je  hel crA crU lss lpc lpa lpc+ lpa+ */

static int meth_noweight[NMETH] =      /* 1 = don't allow weights, just masks */
  { 0 , 1 , 1 , 0 , 1 , 1 , 1 , 0 , 0 , 0 , 0 , 0 , 0  , 0 } ;
/* ls  sp  mi  crM nmi je  hel crA crU lss lpc lpa lpc+ lpa+ */

static int visible_noweights ;

static char *meth_shortname[NMETH] =   /* short names for terse cryptic users */
  { "ls" , "sp" , "mi" , "crM", "nmi", "je"   , "hel",
    "crA", "crU", "lss", "lpc", "lpa", "lpc+" , "lpa+"  } ;

static char *meth_longname[NMETH] =    /* long names for prolix users */
  { "leastsq"         , "spearman"     ,
    "mutualinfo"      , "corratio_mul" ,
    "norm_mutualinfo" , "jointentropy" ,
    "hellinger"       ,
    "corratio_add"    , "corratio_uns" , "signedPcor" ,
    "localPcorSigned" , "localPcorAbs" , "localPcor+Others" , "localPcorAbs+Others" } ;

static char *meth_username[NMETH] =    /* descriptive names */
  { "Least Squares [Pearson Correlation]"   ,
    "Spearman [rank] Correlation"           ,  /* hidden */
    "Mutual Information [H(b)+H(s)-H(b,s)]" ,
    "Correlation Ratio (Symmetrized*)"      ,
    "Normalized MI [H(b,s)/(H(b)+H(s))]"    ,
    "Joint Entropy [H(b,s)]"                ,  /* hidden */
    "Hellinger metric"                      ,
    "Correlation Ratio (Symmetrized+)"      ,
    "Correlation Ratio (Unsym)"             ,
    "Signed Pearson Correlation"            ,  /* hidden */
    "Local Pearson Correlation Signed"      ,  /* hidden */
    "Local Pearson Correlation Abs"         ,  /* hidden */
    "Local Pearson Signed + Others"         ,  /* hidden */
    "Local Pearson Abs + Others"          } ;  /* hidden */

static char *meth_costfunctional[NMETH] =  /* describe cost functional */
  { "1 - abs(Pearson correlation coefficient)"                 ,
    "1 - abs(Spearman correlation coefficient)"                ,
    "- Mutual Information = H(base,source)-H(base)-H(source)"  ,
    "1 - abs[ CR(base,source) * CR(source,base) ]"             ,
    "1/Normalized MI = H(base,source)/[H(base)+H(source)]"     ,
    "H(base,source) = joint entropy of image pair"             ,
    "- Hellinger distance(base,source)"                        ,
    "1 - abs[ CR(base,source) + CR(source,base) ]"             ,
    "CR(source,base) = Var(source|base) / Var(source)"         ,
    "Pearson correlation coefficient between image pair"       ,
    "nonlinear average of Pearson cc over local neighborhoods" ,
    "1 - abs(lpc)"                                             ,
    "lpc + hel + mi + nmi + crA + overlap"                     ,
    "lpa + hel + mi + nmi + crA + overlap"
  } ;

/* check if method code implies use of BLOKs */

#undef  METH_USES_BLOKS
#define METH_USES_BLOKS(mmm) ( mmm == GA_MATCH_PEARSON_LOCALS   || \
                               mmm == GA_MATCH_LPC_MICHO_SCALAR || \
                               mmm == GA_MATCH_LPA_MICHO_SCALAR || \
                               mmm == GA_MATCH_PEARSON_LOCALA       )

/*---------------------------------------------------------------------------*/
/* For the bilinear warp method -- this is obsolete IMHO [RWC] */

#define SETUP_BILINEAR_PARAMS                                                \
 do{ char str[16] ;                                                          \
     stup.wfunc_numpar = NPBIL+4 ;                                           \
     stup.wfunc        = mri_genalign_bilinear ;                             \
     stup.wfunc_param  = (GA_param *)realloc( (void *)stup.wfunc_param,      \
                                              (NPBIL+4)*sizeof(GA_param) ) ; \
     for( jj=12 ; jj < NPBIL ; jj++ ){                                       \
       sprintf(str,"blin%02d",jj+1) ;                                        \
       DEFPAR( jj,str, -nwarp_parmax,nwarp_parmax , 0.0f,0.0f,0.0f ) ;       \
       stup.wfunc_param[jj].fixed = 1 ;                                      \
     }                                                                       \
     DEFPAR(NPBIL  ,"xcen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;                \
     DEFPAR(NPBIL+1,"ycen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;                \
     DEFPAR(NPBIL+2,"zcen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;                \
     DEFPAR(NPBIL+3,"ddfac", 0.0f ,1.0e9 , 1.0f,0.0f,0.0f ) ;                \
     stup.wfunc_param[NPBIL  ].fixed = 2 ;                                   \
     stup.wfunc_param[NPBIL+1].fixed = 2 ;                                   \
     stup.wfunc_param[NPBIL+2].fixed = 2 ;                                   \
     stup.wfunc_param[NPBIL+3].fixed = 2 ;                                   \
 } while(0)

/*---------------------------------------------------------------------------*/

#define OUTVAL(k) stup.wfunc_param[k].val_out

static float BILINEAR_diag_norm(GA_setup stup)
{
   float sum ;
   sum  = fabsf(OUTVAL(12))+fabsf(OUTVAL(13))+fabsf(OUTVAL(14)) ;
   sum += fabsf(OUTVAL(24))+fabsf(OUTVAL(25))+fabsf(OUTVAL(26)) ;
   sum += fabsf(OUTVAL(36))+fabsf(OUTVAL(37))+fabsf(OUTVAL(38)) ;
   return (sum/9.0f) ;
}

static float BILINEAR_offdiag_norm(GA_setup stup)
{
   float dmag ;
   dmag  = fabsf(OUTVAL(15))+fabsf(OUTVAL(16))+fabsf(OUTVAL(17)) ;
   dmag += fabsf(OUTVAL(18))+fabsf(OUTVAL(19))+fabsf(OUTVAL(20)) ;
   dmag += fabsf(OUTVAL(21))+fabsf(OUTVAL(22))+fabsf(OUTVAL(23)) ;
   dmag += fabsf(OUTVAL(27))+fabsf(OUTVAL(28))+fabsf(OUTVAL(29)) ;
   dmag += fabsf(OUTVAL(30))+fabsf(OUTVAL(31))+fabsf(OUTVAL(32)) ;
   dmag += fabsf(OUTVAL(33))+fabsf(OUTVAL(34))+fabsf(OUTVAL(35)) ;
   return (dmag/18.0f) ;
}

/*---------------------------------------------------------------------------*/
/* for the polynomial warp setup - also obsolete [RWC] */

#define SETUP_POLYNO_PARAMS(nnl,ran,nam)                                     \
 do{ char str[32] , *spt , xyz[3] = { 'x', 'y', 'z' } ;                     \
     stup.wfunc_numpar = 16+(nnl) ;                                          \
     stup.wfunc        = NULL ;                                              \
     stup.wfunc_param  = (GA_param *)realloc( (void *)stup.wfunc_param,      \
                                              (16+(nnl))*sizeof(GA_param) ); \
     for( jj=12 ; jj < 12+(nnl) ; jj++ ){                                    \
       spt = GA_polywarp_funcname( (jj-12)/3 ) ;                             \
       if( spt != NULL ) sprintf(str,"%s:%s:%c"  ,(nam),spt ,xyz[jj%3]) ;    \
       else              sprintf(str,"%s:%03d:%c",(nam),jj+1,xyz[jj%3]) ;    \
       DEFPAR( jj,str, -(ran),(ran) , 0.0f,0.0f,0.0f ) ;                     \
       stup.wfunc_param[jj].fixed = 1 ;                                      \
     }                                                                       \
     DEFPAR(12+(nnl),"xcen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;               \
     DEFPAR(13+(nnl),"ycen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;               \
     DEFPAR(14+(nnl),"zcen" ,-1.0e9,1.0e9 , 0.0f,0.0f,0.0f ) ;               \
     DEFPAR(15+(nnl),"xxfac", 0.0f ,1.0e9 , 1.0f,0.0f,0.0f ) ;               \
     stup.wfunc_param[12+(nnl)].fixed = 2 ;                                  \
     stup.wfunc_param[13+(nnl)].fixed = 2 ;                                  \
     stup.wfunc_param[14+(nnl)].fixed = 2 ;                                  \
     stup.wfunc_param[15+(nnl)].fixed = 2 ;                                  \
 } while(0)

#define SETUP_CUBIC_PARAMS do{ SETUP_POLYNO_PARAMS(48,nwarp_parmax,"cubic");  \
                               stup.wfunc = mri_genalign_cubic ; } while(0)

#define SETUP_QUINT_PARAMS do{ SETUP_POLYNO_PARAMS(156,nwarp_parmax,"quint"); \
                               stup.wfunc = mri_genalign_quintic ; } while(0)

#define SETUP_HEPT_PARAMS do{ SETUP_POLYNO_PARAMS(348,nwarp_parmax,"heptic"); \
                              stup.wfunc = mri_genalign_heptic ; } while(0)

#define SETUP_NONI_PARAMS do{ SETUP_POLYNO_PARAMS(648,nwarp_parmax,"nonic");  \
                              stup.wfunc = mri_genalign_nonic ; } while(0)

/*---------- Macros for freezing motions in particular directions ----------*/

/* cc = 1,2,3 for x,y,z directions:
   permanently freeze parameters that cause motion in that direction */

#define FREEZE_POLYNO_PARAMS_MOT(cc)                           \
  do{ int pp ;                                                 \
      for( pp=12 ; pp < stup.wfunc_numpar ; pp++ ){            \
        if( 1+pp%3 == (cc) ) stup.wfunc_param[pp].fixed = 2 ;  \
      }                                                        \
  } while(0)
/* cc = 1,2,3 for x,y,z directions:
   freeze parameters whose basis funcs are dependent on that coordinate */

#define FREEZE_POLYNO_PARAMS_DEP(cc)                           \
  do{ int pp , qq , cm=(1 << ((cc)-1)) ;                       \
      for( pp=12 ; pp < stup.wfunc_numpar ; pp++ ){            \
        qq = GA_polywarp_coordcode( (pp-12)/3 ) ;              \
        if( qq & cm ) stup.wfunc_param[pp].fixed = 2 ;         \
      }                                                        \
  } while(0)

/* overall parameter freeze box, based on user options */

#define FREEZE_POLYNO_PARAMS                                   \
 do{ if( nwarp_fixmotX ) FREEZE_POLYNO_PARAMS_MOT(1) ;         \
     if( nwarp_fixmotY ) FREEZE_POLYNO_PARAMS_MOT(2) ;         \
     if( nwarp_fixmotZ ) FREEZE_POLYNO_PARAMS_MOT(3) ;         \
     if( nwarp_fixdepX ) FREEZE_POLYNO_PARAMS_DEP(1) ;         \
     if( nwarp_fixdepY ) FREEZE_POLYNO_PARAMS_DEP(2) ;         \
     if( nwarp_fixdepZ ) FREEZE_POLYNO_PARAMS_DEP(3) ; } while(0)

/*----------- count free params into variable 'nf' -----------*/

#define COUNT_FREE_PARAMS(nf)                                  \
 do{ int jj ;                                                  \
     if( verb > 4 ) fprintf(stderr,"++ Free params:") ;        \
     for( (nf)=jj=0 ; jj < stup.wfunc_numpar ; jj++ ){         \
       if( !stup.wfunc_param[jj].fixed ){                      \
         (nf)++ ; if( verb > 4 ) fprintf(stderr," %d",jj) ;    \
       }                                                       \
     }                                                         \
     if( verb > 4 ) fprintf(stderr,"\n") ;                     \
 } while(0)

/*--------- Macro to save Pearson map (for use in main) [27 Jan 2021] --------*/

#define SAVE_PEARSON_MAP(sprefix,val_xxx)                                        \
  do{ MRI_IMAGE *pim ;                                                           \
      PAR_CPY(val_xxx) ;           /* copy output parameters to allpar[] */      \
      pim = mri_genalign_map_pearson_local(&stup,allpar) ; /* get 3D map */      \
      if( pim != NULL ){                                                         \
        THD_3dim_dataset *pset ;                                                 \
        pset = THD_volume_to_dataset( dset_base , pim , /* convert to dataset */ \
                                      (sprefix) ,   pad_xm,pad_xp,               \
                                      pad_ym,pad_yp,pad_zm,pad_zp ) ;            \
        mri_free(pim) ;                                                          \
        if( pset != NULL ){  /* save dataset */                                  \
          DSET_write(pset) ; WROTE_DSET(pset) ; DSET_delete(pset) ;              \
        } else {                                                                 \
          ERROR_message("Failed to create -PearSave dataset %s :(",(sprefix)) ;  \
        }                                                                        \
      } else {                                                                   \
          ERROR_message("Failed to create -PearSave volume %s :(",(sprefix)) ;   \
      }                                                                          \
  } while(0)

/*---------------------------------------------------------------------------*/
/* Given a string, find the cost functional method code that goes with it. */

int meth_name_to_code( char *nam )  /* 15 Dec 2010 */
{
   int ii ;
   if( nam == NULL || *nam == '\0' ) return 0 ;
   for( ii=0 ; ii < NMETH ; ii++ ){
     if( strcasecmp (nam,meth_shortname[ii])  == 0 ) return (ii+1) ;
     if( strncasecmp(nam,meth_longname[ii],7) == 0 ) return (ii+1) ;
   }
   return 0 ;
}

/*---------------------------------------------------------------------------*/
/* Given an interpolation code, find the name that goes with it. */

char * INTERP_methname( int iii )
{
   switch( iii ){
     default:          return "UNKNOWN" ;
     case MRI_NN:      return "NN"      ;
     case MRI_LINEAR:  return "linear"  ;
     case MRI_CUBIC:   return "cubic"   ;
     case MRI_QUINTIC: return "quintic" ;
     case MRI_HEPTIC:  return "heptic"  ;
     case MRI_WSINC5:  return "wsinc5"  ;
   }
   return "MYSTERIOUS" ; /* unreachable */
}

/*---------------------------- And vice-versa ------------------------------*/

int INTERP_code( char *name )
{
   if( name == NULL ) return -1 ;
   if( strcmp(name,"NN") == 0 || strncmp(name,"nearest",5) == 0 )
     return MRI_NN ;
   else
   if( strncmp(name,"linear",3) == 0 || strncmp(name,"trilinear",5) == 0 )
     return MRI_LINEAR ;
   else
   if( strncmp(name,"cubic",3) == 0 || strncmp(name,"tricubic",5) == 0 )
     return MRI_CUBIC ;
   else
   if( strncmp(name,"quintic",3)==0 || strncmp(name,"triquintic",5)==0 )
     return MRI_QUINTIC ;
   else
   if( strncasecmp(name,"WSINC",5)==0 )
     return MRI_WSINC5 ;

   return -1 ;
}

/*---------------------------------------------------------------------------*/

void Allin_Help(void)  /* moved here 15 Mar 2021 */
{
   int ii ;

     visible_noweights = 0 ;
     for( ii=0 ; ii < NMETH ; ii++ )
       if( meth_visible[ii] && meth_noweight[ii] ) visible_noweights++ ;

     printf("\n"
     "Usage: 3dAllineate [options] sourcedataset\n"
     "\n"
     "--------------------------------------------------------------------------\n"
     "      Program to align one dataset (the 'source') to a 'base'\n"
     "      dataset, using an affine (matrix) transformation of space.\n"
     "--------------------------------------------------------------------------\n"
     "\n"
     "--------------------------------------------------------------------------\n"
     "    ***** Please check your results visually, or at some point  *****\n"
     "    ***** in time you will have bad results and not know it :-( *****\n"
     "    *****                                                       *****\n"
     "    ***** No method for 3D image alignment, however tested it   *****\n"
     "    ***** was, can be relied upon 100%% of the time, and anyone  *****\n"
     "    ***** who tells you otherwise is a madman or is a liar!!!!  *****\n"
     "    *****                                                       *****\n"
     "    ***** Furthermore, don't EVER think that \"I have so much    *****\n"
     "    ***** data that a few errors will not matter\"!!!!           *****\n"
     "--------------------------------------------------------------------------\n"
     "\n"
     "* Options (lots of them!) are available to control:\n"
     " ++ How the matching between the source and the base is computed\n"
     "    (i.e., the 'cost functional' measuring image mismatch).\n"
     " ++ How the resliced source is interpolated to the base space.\n"
     " ++ The complexity of the spatial transformation ('warp') used.\n"
     " ++ And many many technical options to control the process in detail,\n"
     "    if you know what you are doing (or just like to fool around).\n"
     "\n"
     "* This program is a generalization of and improvement on the older\n"
     "    software 3dWarpDrive.\n"
     "\n"
     "* For nonlinear transformations, see progam 3dQwarp.\n"
     "\n"
     "* 3dAllineate can also be used to apply a pre-computed matrix to a dataset\n"
     "  to produce the transformed output. In this mode of operation, it just\n"
     "  skips the alignment process, whose function is to compute the matrix,\n"
     "  and instead it reads the matrix in, computes the output dataset,\n"
     "  writes it out, and stops.\n"
     "\n"
     "* If you are curious about the stepwise process used, see the section below\n"
     "  titled 'SUMMARY of the Default Allineation Process'.\n"
     "\n"
     "=====----------------------------------------------------------------------\n"
     "NOTES: For most 3D image registration purposes, we now recommend that you\n"
     "=====  use Daniel Glen's script align_epi_anat.py (which, despite its name,\n"
     "       can do many more registration problems than EPI-to-T1-weighted).\n"
     "  -->> In particular, using 3dAllineate with the 'lpc' cost functional\n"
     "       (to align EPI and T1-weighted volumes) requires using a '-weight'\n"
     "       volume to get good results, and the align_epi_anat.py script will\n"
     "       automagically generate such a weight dataset that works well for\n"
     "       EPI-to-structural alignment.\n"
     "  -->> This script can also be used for other alignment purposes, such\n"
     "       as T1-weighted alignment between field strengths using the\n"
     "       '-lpa' cost functional.  Investigate align_epi_anat.py to\n"
     "       see if it will do what you need -- you might make your life\n"
     "       a little easier and nicer and happier and more tranquil.\n"
     "  -->> Also, if/when you ask for registration help on the AFNI\n"
     "       message board, we'll probably start by recommending that you\n"
     "       try align_epi_anat.py if you haven't already done so.\n"
     "  -->> For aligning EPI and T1-weighted volumes, we have found that\n"
     "       using a flip angle of 50-60 degrees for the EPI works better than\n"
     "       a flip angle of 90 degrees.  The reason is that there is more\n"
     "       internal contrast in the EPI data when the flip angle is smaller,\n"
     "       so the registration has some image structure to work with.  With\n"
     "       the 90 degree flip angle, there is so little internal contrast in\n"
     "       the EPI dataset that the alignment process ends up being just\n"
     "       trying to match brain outlines -- which doesn't always give accurate\n"
     "       results: see http://dx.doi.org/10.1016/j.neuroimage.2008.09.037\n"
     "  -->> Although the total MRI signal is reduced at a smaller flip angle,\n"
     "       there is little or no loss in FMRI/BOLD information, since the bulk\n"
     "       of the time series 'noise' is from physiological fluctuation signals,\n"
     "       which are also reduced by the lower flip angle -- for more details,\n"
     "       see http://dx.doi.org/10.1016/j.neuroimage.2010.11.020\n"
     "---------------------------------------------------------------------------\n"
     "  **** New (Summer 2013) program 3dQwarp is available to do nonlinear  ****\n"
     "  ***  alignment between a base and source dataset, including the use   ***\n"
     "  **   of 3dAllineate for the preliminary affine alignment.  If you are  **\n"
     "  *    interested, see the output of '3dQwarp -help' for the details.     *\n"
     "---------------------------------------------------------------------------\n"
     "\n"
     "COMMAND LINE OPTIONS:\n"
     "====================\n"
     " -base bbb   = Set the base dataset to be the #0 sub-brick of 'bbb'.\n"
     "               If no -base option is given, then the base volume is\n"
     "               taken to be the #0 sub-brick of the source dataset.\n"
     "               (Base must be stored as floats, shorts, or bytes.)\n"
     "            ** -base is not needed if you are just applying a given\n"
     "               transformation to the -source dataset to produce\n"
     "               the output, using -1Dmatrix_apply or -1Dparam_apply\n"
     "            ** Unless you use the -master option, the aligned\n"
     "               output dataset will be stored on the same 3D grid\n"
     "               as the -base dataset.\n"
     "\n"
     " -source ttt = Read the source dataset from 'ttt'.  If no -source\n"
     "   *OR*        (or -input) option is given, then the source dataset\n"
     " -input ttt    is the last argument on the command line.\n"
     "               (Source must be stored as floats, shorts, or bytes.)\n"
     "            ** This is the dataset to be transformed, to match the\n"
     "               -base dataset, or directly with one of the options\n"
     "               -1Dmatrix_apply or -1Dparam_apply\n"
     "            ** 3dAllineate can register 2D datasets (single slice),\n"
     "               but both the base and source must be 2D -- you cannot\n"
     "               use this program to register a 2D slice into a 3D volume!\n"
     "               -- However, the 'lpc' and 'lpa' cost functionals do not\n"
     "                  work properly with 2D images, as they are designed\n"
     "                  around local 3D neighborhoods and that code has not\n"
     "                  been patched to work with 2D neighborhoods :(\n"
     "               -- You can input .jpg files as 2D 'datasets', register\n"
     "                  them with 3dAllineate, and write the result back out\n"
     "                  using a prefix that ends in '.jpg'; HOWEVER, the color\n"
     "                  information will not be used in the registration, as\n"
     "                  this program was written to deal with monochrome medical\n"
     "                  datasets. At the end, if the source was RGB (color), then\n"
     "                  the output will be also be RGB, and then a color .jpg\n"
     "                  can be output.\n"
     "               -- The above remarks also apply to aligning 3D RGB datasets:\n"
     "                  it will be done using only the 3D volumes converted to\n"
     "                  grayscale, but the final output will be the source\n"
     "                  RGB dataset transformed to the (hopefully) aligned grid.\n"
     "                 * However, I've never tested aligning 3D color datasets;\n"
     "                   you can be the first one ever!\n"
     "            ** See the script @2dwarper.Allin for an example of using\n"
     "               3dAllineate to do slice-by-slice nonlinear warping to\n"
     "               align 3D volumes distorted by time-dependent magnetic\n"
     "               field inhomogeneities.\n"
     "\n"
     " ** NOTA BENE: The base and source dataset do NOT have to be defined **\n"
     " ** [that's]   on the same 3D grids; the alignment process uses the  **\n"
     " ** [Latin ]   coordinate systems defined in the dataset headers to  **\n"
     " ** [  for ]   make the match between spatial locations, rather than **\n"
     " ** [ NOTE ]   matching the 2 datasets on a voxel-by-voxel basis     **\n"
     " ** [ WELL ]   (as 3dvolreg and 3dWarpDrive do).                     **\n"
     " **       -->> However, this coordinate-based matching requires that **\n"
     " **            image volumes be defined on roughly the same patch of **\n"
     " **            of (x,y,z) space, in order to find a decent starting  **\n"
     " **            point for the transformation.  You might need to use  **\n"
     " **            the script @Align_Centers to do this, if the 3D       **\n"
     " **            spaces occupied by the images do not overlap much.    **\n"
     " **       -->> Or the '-cmass' option to this program might be       **\n"
     " **            sufficient to solve this problem, maybe, with luck.   **\n"
     " **            (Another reason why you should use align_epi_anat.py) **\n"
     " **       -->> If the coordinate system in the dataset headers is    **\n"
     " **            WRONG, then 3dAllineate will probably not work well!  **\n"
     " **            And I say this because we have seen this in several   **\n"
     " **            datasets downloaded from online archives.             **\n"
     "\n"
     " -prefix ppp = Output the resulting dataset to file 'ppp'.  If this\n"
     "   *OR*        option is NOT given, no dataset will be output!  The\n"
     " -out ppp      transformation matrix to align the source to the base will\n"
     "               be estimated, but not applied.  You can save the matrix\n"
     "               for later use using the '-1Dmatrix_save' option.\n"
     "        *N.B.: By default, the new dataset is computed on the grid of the\n"
     "                base dataset; see the '-master' and/or the '-mast_dxyz'\n"
     "                options to change this grid.\n"
     "        *N.B.: If 'ppp' is 'NULL', then no output dataset will be produced.\n"
     "                This option is for compatibility with 3dvolreg.\n"
     "\n"
     " -floatize   = Write result dataset as floats.  Internal calculations\n"
     " -float        are all done on float copies of the input datasets.\n"
     "               [Default=convert output dataset to data format of  ]\n"
     "               [        source dataset; if the source dataset was ]\n"
     "               [        shorts with a scale factor, then the new  ]\n"
     "               [        dataset will get a scale factor as well;  ]\n"
     "               [        if the source dataset was shorts with no  ]\n"
     "               [        scale factor, the result will be unscaled.]\n"
     "\n"
     " -1Dparam_save ff   = Save the warp parameters in ASCII (.1D) format into\n"
     "                      file 'ff' (1 row per sub-brick in source).\n"
     "                    * A historical synonym for this option is '-1Dfile'.\n"
     "                    * At the top of the saved 1D file is a #comment line\n"
     "                      listing the names of the parameters; those parameters\n"
     "                      that are fixed (e.g., via '-parfix') will be marked\n"
     "                      by having their symbolic names end in the '$' character.\n"
     "                      You can use '1dcat -nonfixed' to remove these columns\n"
     "                      from the 1D file if you just want to further process the\n"
     "                      varying parameters somehow (e.g., 1dsvd).\n"
     "                    * However, the '-1Dparam_apply' option requires the\n"
     "                      full list of parameters, including those that were\n"
     "                      fixed, in order to work properly!\n"
     "\n"
     " -1Dparam_apply aa  = Read warp parameters from file 'aa', apply them to \n"
     "                      the source dataset, and produce a new dataset.\n"
     "                      (Must also use the '-prefix' option for this to work!  )\n"
     "                      (In this mode of operation, there is no optimization of)\n"
     "                      (the cost functional by changing the warp parameters;  )\n"
     "                      (previously computed parameters are applied directly.  )\n"
/** "               *N.B.: A historical synonym for this is '-1Dapply'.\n" **/
     "               *N.B.: If you use -1Dparam_apply, you may also want to use\n"
     "                       -master to control the grid on which the new\n"
     "                       dataset is written -- the base dataset from the\n"
     "                       original 3dAllineate run would be a good possibility.\n"
     "                       Otherwise, the new dataset will be written out on the\n"
     "                       3D grid coverage of the source dataset, and this\n"
     "                       might result in clipping off part of the image.\n"
     "               *N.B.: Each row in the 'aa' file contains the parameters for\n"
     "                       transforming one sub-brick in the source dataset.\n"
     "                       If there are more sub-bricks in the source dataset\n"
     "                       than there are rows in the 'aa' file, then the last\n"
     "                       row is used repeatedly.\n"
     "               *N.B.: A trick to use 3dAllineate to resample a dataset to\n"
     "                       a finer grid spacing:\n"
     "                         3dAllineate -input dataset+orig         \\\n"
     "                                     -master template+orig       \\\n"
     "                                     -prefix newdataset          \\\n"
     "                                     -final wsinc5               \\\n"
     "                                     -1Dparam_apply '1D: 12@0'\\'  \n"
     "                       Here, the identity transformation is specified\n"
     "                       by giving all 12 affine parameters as 0 (note\n"
     "                       the extra \\' at the end of the '1D: 12@0' input!).\n"
     "                     ** You can also use the word 'IDENTITY' in place of\n"
     "                        '1D: 12@0'\\' (to indicate the identity transformation).\n"
     "              **N.B.: Some expert options for modifying how the wsinc5\n"
     "                       method works are described far below, if you use\n"
     "                       '-HELP' instead of '-help'.\n"
     "            ****N.B.: The interpolation method used to produce a dataset\n"
     "                       is always given via the '-final' option, NOT via\n"
     "                       '-interp'.  If you forget this and use '-interp'\n"
     "                       along with one of the 'apply' options, this program\n"
     "                       will chastise you (gently) and change '-final'\n"
     "                       to match what the '-interp' input.\n"
     "\n"
     " -1Dmatrix_save ff  = Save the transformation matrix for each sub-brick into\n"
     "                      file 'ff' (1 row per sub-brick in the source dataset).\n"
     "                      If 'ff' does NOT end in '.1D', then the program will\n"
     "                      append '.aff12.1D' to 'ff' to make the output filename.\n"
     "               *N.B.: This matrix is the coordinate transformation from base\n"
     "                       to source DICOM coordinates. In other terms:\n"
     "                          Xin = Xsource = M Xout = M Xbase\n"
     "                                   or\n"
     "                          Xout = Xbase = inv(M) Xin = inv(M) Xsource\n"
     "                       where Xin or Xsource is the 4x1 coordinates of a\n"
     "                       location in the input volume. Xout is the \n"
     "                       coordinate of that same location in the output volume.\n"
     "                       Xbase is the coordinate of the corresponding location\n"
     "                       in the base dataset. M is ff augmented by a 4th row of\n"
     "                       [0 0 0 1], X. is an augmented column vector [x,y,z,1]'\n"
     "                       To get the inverse matrix inv(M)\n"
     "                       (source to base), use the cat_matvec program, as in\n"
     "                         cat_matvec fred.aff12.1D -I\n"
     "\n"
     " -1Dmatrix_apply aa = Use the matrices in file 'aa' to define the spatial\n"
     "                      transformations to be applied.  Also see program\n"
     "                      cat_matvec for ways to manipulate these matrix files.\n"
     "               *N.B.: You probably want to use either -base or -master\n"
     "                      with either *_apply option, so that the coordinate\n"
     "                      system that the matrix refers to is correctly loaded.\n"
     "                     ** You can also use the word 'IDENTITY' in place of a\n"
     "                        filename to indicate the identity transformation --\n"
     "                        presumably for the purpose of resampling the source\n"
     "                        dataset to a new grid.\n"
     "\n"
     "  * The -1Dmatrix_* options can be used to save and re-use the transformation *\n"
     "  * matrices.  In combination with the program cat_matvec, which can multiply *\n"
     "  * saved transformation matrices, you can also adjust these matrices to      *\n"
     "  * other alignments. These matrices can also be combined with nonlinear      *\n"
     "  * warps (from 3dQwarp) using programs 3dNwarpApply or 3dNwarpCat.           *\n"
     "\n"
     "  * The script 'align_epi_anat.py' uses 3dAllineate and 3dvolreg to align EPI *\n"
     "  * datasets to T1-weighted anatomical datasets, using saved matrices between *\n"
     "  * the two programs.  This script is our currently recommended method for    *\n"
     "  * doing such intra-subject alignments.                                      *\n"
     "\n"
     " -cost ccc   = Defines the 'cost' function that defines the matching\n"
     "               between the source and the base; 'ccc' is one of\n"
      ) ;

      for( ii=0 ; ii < NMETH ; ii++ )
        if( meth_visible[ii] )
          printf( "                %-4s *OR*  %-16s= %s\n" ,
                  meth_shortname[ii] , meth_longname[ii] , meth_username[ii] ) ;

      printf(
     "               You can also specify the cost functional using an option\n"
     "               of the form '-mi' rather than '-cost mi', if you like\n"
     "               to keep things terse and cryptic (as I do).\n"
     "               [Default == '-hel' (for no good reason, but it sounds nice).]\n"
     "               **NB** See more below about lpa and lpc, which are typically\n"
     "                      what we would recommend as first-choice cost functions\n"
     "                      now:\n"
     "                        lpa if you have similar contrast vols to align;\n"
     "                        lpc if you have *non*similar contrast vols to align!\n"
     "\n"
     " -interp iii = Defines interpolation method to use during matching\n"
     "               process, where 'iii' is one of\n"
     "                 NN      *OR* nearestneighbour *OR nearestneighbor\n"
     "                 linear  *OR* trilinear\n"
     "                 cubic   *OR* tricubic\n"
     "                 quintic *OR* triquintic\n"
     "               Using '-NN' instead of '-interp NN' is allowed (e.g.).\n"
     "               Note that using cubic or quintic interpolation during\n"
     "               the matching process will slow the program down a lot.\n"
     "               Use '-final' to affect the interpolation method used\n"
     "               to produce the output dataset, once the final registration\n"
     "               parameters are determined.  [Default method == 'linear'.]\n"
     "            ** N.B.: Linear interpolation is used during the coarse\n"
     "                     alignment pass; the selection here only affects\n"
     "                     the interpolation method used during the second\n"
     "                     (fine) alignment pass.\n"
     "            ** N.B.: '-interp' does NOT define the final method used\n"
     "                     to produce the output dataset as warped from the\n"
     "                     input dataset.  If you want to do that, use '-final'.\n"
     "\n"
     " -final iii  = Defines the interpolation mode used to create the\n"
     "               output dataset.  [Default == 'cubic']\n"
     "            ** N.B.: If you are applying a transformation to an\n"
     "                       integer-valued dataset (such as an atlas),\n"
     "                       then you should use '-final NN' to avoid\n"
     "                       interpolation of the integer labels.\n"
     "            ** N.B.: For '-final' ONLY, you can use 'wsinc5' to specify\n"
     "                       that the final interpolation be done using a\n"
     "                       weighted sinc interpolation method.  This method\n"
     "                       is so SLOW that you aren't allowed to use it for\n"
     "                       the registration itself.\n"
     "                  ++ wsinc5 interpolation is highly accurate and should\n"
     "                       reduce the smoothing artifacts from lower\n"
     "                       order interpolation methods (which are most\n"
     "                       visible if you interpolate an EPI time series\n"
     "                       to high resolution and then make an image of\n"
     "                       the voxel-wise variance).\n"
     "                  ++ On my Intel-based Mac, it takes about 2.5 s to do\n"
     "                       wsinc5 interpolation, per 1 million voxels output.\n"
     "                       For comparison, quintic interpolation takes about\n"
     "                       0.3 s per 1 million voxels: 8 times faster than wsinc5.\n"
     "                  ++ The '5' refers to the width of the sinc interpolation\n"
     "                       weights: plus/minus 5 grid points in each direction;\n"
     "                       this is a tensor product interpolation, for speed.\n"
     "\n"
     "TECHNICAL OPTIONS (used for fine control of the program):\n"
     "=================\n"
     " -nmatch nnn = Use at most 'nnn' scattered points to match the\n"
     "               datasets.  The smaller nnn is, the faster the matching\n"
     "               algorithm will run; however, accuracy may be bad if\n"
     "               nnn is too small.  If you end the 'nnn' value with the\n"
     "               '%%' character, then that percentage of the base's\n"
     "               voxels will be used.\n"
     "               [Default == 47%% of voxels in the weight mask]\n"
     "\n"
     " -nopad      = Do not use zero-padding on the base image.\n"
     "               (I cannot think of a good reason to use this option.)\n"
     "               [Default == zero-pad, if needed; -verb shows how much]\n"
     "\n"
     " -zclip      = Replace negative values in the input datasets (source & base)\n"
     " -noneg        with zero.  The intent is to clip off a small set of negative\n"
     "               values that may arise when using 3dresample (say) with\n"
     "               cubic interpolation.\n"
     "\n"
     " -conv mmm   = Convergence test is set to 'mmm' millimeters.\n"
     "               This doesn't mean that the results will be accurate\n"
     "               to 'mmm' millimeters!  It just means that the program\n"
     "               stops trying to improve the alignment when the optimizer\n"
     "               (NEWUOA) reports it has narrowed the search radius\n"
     "               down to this level.\n"
     "               * To set this value to the smallest allowable, use '-conv 0'.\n"
     "               * A coarser value for 'quick-and-dirty' alignment is 0.05.\n"
     "\n"
     " -verb       = Print out verbose progress reports.\n"
     "               [Using '-VERB' will give even more prolix reports :]\n"
     " -quiet      = Don't print out verbose stuff. (But WHY?)\n"
     "\n"
     " -usetemp    = Write intermediate stuff to disk, to economize on RAM.\n"
     "               Using this will slow the program down, but may make it\n"
     "               possible to register datasets that need lots of space.\n"
     "       **N.B.: Temporary files are written to the directory given\n"
     "               in environment variable TMPDIR, or in /tmp, or in ./\n"
     "               (preference in that order).  If the program crashes,\n"
     "               these files are named TIM_somethingrandom, and you\n"
     "               may have to delete them manually. (TIM=Temporary IMage)\n"
     "       **N.B.: If the program fails with a 'malloc failure' type of\n"
     "               message, then try '-usetemp' (malloc=memory allocator).\n"
     "             * If the program just stops with a message 'killed', that\n"
     "               means the operating system (Unix/Linux) stopped the\n"
     "               program, which almost always is due to the system running\n"
     "               low on memory -- so it starts killing programs to save itself.\n"
#ifdef USING_MCW_MALLOC
     "       **N.B.: If you use '-verb', then memory usage is printed out\n"
     "               at various points along the way.\n"
#endif
     "\n"
     " -nousetemp  = Don't use temporary workspace on disk [the default].\n"
     "\n"
#ifdef ALLOW_METH_CHECK
     " -check hhh  = After cost functional optimization is done, start at the\n"
     "               final parameters and RE-optimize using the new cost\n"
     "               function 'hhh'.  If the results are too different, a\n"
     "               warning message will be printed.  However, the final\n"
     "               parameters from the original optimization will be\n"
     "               used to create the output dataset. Using '-check'\n"
     "               increases the CPU time, but can help you feel sure\n"
     "               that the alignment process did not go wild and crazy.\n"
     "               [Default == no check == don't worry, be happy!]\n"
     "       **N.B.: You can put more than one function after '-check', as in\n"
     "                 -nmi -check mi hel crU crM\n"
     "               to register with Normalized Mutual Information, and\n"
     "               then check the results against 4 other cost functionals.\n"
     "       **N.B.: On the other hand, some cost functionals give better\n"
     "               results than others for specific problems, and so\n"
     "               a warning that 'mi' was significantly different than\n"
     "               'hel' might not actually mean anything useful (e.g.).\n"
#if 0
     "       **N.B.: If you use '-CHECK' instead of '-check', AND there are\n"
     "               at least two extra check functions specified (in addition\n"
     "               to the primary cost functional), then the output parameter\n"
     "               set will be the median of all the final parameter sets\n"
     "               generated at this stage (including the primary set).\n"
     "                 **** '-CHECK' is experimental and CPU intensive ****\n"
#endif
#endif
     "\n"
     " ** PARAMETERS THAT AFFECT THE COST OPTIMIZATION STRATEGY **\n"
     "\n"
     " -onepass    = Use only the refining pass -- do not try a coarse\n"
     "               resolution pass first.  Useful if you know that only\n"
     "               SMALL amounts of image alignment are needed.\n"
     "               [The default is to use both passes.]\n"
     "\n"
     " -twopass    = Use a two pass alignment strategy, first searching for\n"
     "               a large rotation+shift and then refining the alignment.\n"
     "               [Two passes are used by default for the first sub-brick]\n"
     "               [in the source dataset, and then one pass for the others.]\n"
     "               ['-twopass' will do two passes for ALL source sub-bricks.]\n"
     "            *** The first (coarse) pass is relatively slow, as it tries\n"
     "                 to search a large volume of parameter (rotations+shifts)\n"
     "                 space for initial guesses at the alignment transformation.\n"
     "              * A lot of these initial guesses are kept and checked to\n"
     "                 see which ones lead to good starting points for the\n"
     "                 further refinement.\n"
     "              * The winners of this competition are then passed to the\n"
     "                 '-twobest' (infra) successive optimization passes.\n"
     "              * The ultimate winner of THAT stage is what starts\n"
     "                 the second (fine) pass alignment. Usually, this starting\n"
     "                 point is so good that the fine pass optimization does\n"
     "                 not provide a lot of improvement; that is, most of the\n"
     "                 run time ends up in coarse pass with its multiple stages.\n"
     "              * All of these stages are intended to help the program avoid\n"
     "                 stopping at a 'false' minimum in the cost functional.\n"
     "                 They were added to the software as we gathered experience\n"
     "                 with difficult 3D alignment problems. The combination of\n"
     "                 multiple stages of partial optimization of multiple\n"
     "                 parameter candidates makes the coarse pass slow, but also\n"
     "                 makes it (usually) work well.\n"
     "\n"
     " -twoblur rr = Set the blurring radius for the first pass to 'rr'\n"
     "               millimeters.  [Default == 11 mm]\n"
     "       **N.B.: You may want to change this from the default if\n"
     "               your voxels are unusually small or unusually large\n"
     "               (e.g., outside the range 1-4 mm along each axis).\n"
     "\n"
     " -twofirst   = Use -twopass on the first image to be registered, and\n"
     "               then on all subsequent images from the source dataset,\n"
     "               use results from the first image's coarse pass to start\n"
     "               the fine pass.\n"
     "               (Useful when there may be large motions between the   )\n"
     "               (source and the base, but only small motions within   )\n"
     "               (the source dataset itself; since the coarse pass can )\n"
     "               (be slow, doing it only once makes sense in this case.)\n"
     "       **N.B.: [-twofirst is on by default; '-twopass' turns it off.]\n"
     "\n"
     " -twobest bb = In the coarse pass, use the best 'bb' set of initial\n"
     "               points to search for the starting point for the fine\n"
     "               pass.  If bb==0, then no search is made for the best\n"
     "               starting point, and the identity transformation is\n"
     "               used as the starting point.  [Default=%d; min=0 max=%d]\n"
     "       **N.B.: Setting bb=0 will make things run faster, but less reliably.\n"
     "\n"
     " -fineblur x = Set the blurring radius to use in the fine resolution\n"
     "               pass to 'x' mm.  A small amount (1-2 mm?) of blurring at\n"
     "               the fine step may help with convergence, if there is\n"
     "               some problem, especially if the base volume is very noisy.\n"
     "               [Default == 0 mm = no blurring at the final alignment pass]\n"
     "\n"
     "   **NOTES ON\n"
     "   **STRATEGY: * If you expect only small-ish (< 2 voxels?) image movement,\n"
     "                 then using '-onepass' or '-twobest 0' makes sense.\n"
     "               * If you expect large-ish image movements, then do not\n"
     "                 use '-onepass' or '-twobest 0'; the purpose of the\n"
     "                 '-twobest' parameter is to search for large initial\n"
     "                 rotations/shifts with which to start the coarse\n"
     "                 optimization round.\n"
     "               * If you have multiple sub-bricks in the source dataset,\n"
     "                 then the default '-twofirst' makes sense if you don't expect\n"
     "                 large movements WITHIN the source, but expect large motions\n"
     "                 between the source and base.\n"
     "               * '-twopass' re-starts the alignment process for each sub-brick\n"
     "                 in the source dataset -- this option can be time consuming,\n"
     "                 and is really intended to be used when you might expect large\n"
     "                 movements between sub-bricks; for example, when the different\n"
     "                 volumes are gathered on different days.  For most purposes,\n"
     "                 '-twofirst' (the default process) will be adequate and faster,\n"
     "                 when operating on multi-volume source datasets.\n"

       , DEFAULT_TBEST , PARAM_MAXTRIAL  /* for -twobest */
      ) ;

      printf(
     "\n"
     " -cmass        = Use the center-of-mass calculation to determin an initial shift\n"
     "                   [This option is OFF by default]\n"
     "                 can be given as cmass+a, cmass+xy, cmass+yz, cmass+xz\n"
     "                 where +a means to try determine automatically in which\n"
     "                 direction the data is partial by looking for a too large shift\n"
     "                 If given in the form '-cmass+xy' (for example), means to\n"
     "                 do the CoM calculation in the x- and y-directions, but\n"
     "                 not the z-direction.\n"
     "               * MY OPINION: This option is REALLY useful in most cases.\n"
     "                             However, if you only have partial coverage in\n"
     "                             the -source dataset, you will need to use\n"
     "                             one of the '+' additions to restrict the\n"
     "                             use of the CoM limits.\n"
     "\n"
     " -nocmass      = Don't use the center-of-mass calculation. [The default]\n"
     "                  (You would not want to use the C-o-M calculation if the  )\n"
     "                  (source sub-bricks have very different spatial locations,)\n"
     "                  (since the source C-o-M is calculated from all sub-bricks)\n"
     "\n"
     " **EXAMPLE: You have a limited coverage set of axial EPI slices you want to\n"
     "            register into a larger head volume (after 3dSkullStrip, of course).\n"
     "            In this case, '-cmass+xy' makes sense, allowing CoM adjustment\n"
     "            along the x = R-L and y = A-P directions, but not along the\n"
     "            z = I-S direction, since the EPI doesn't cover the whole brain\n"
     "            along that axis.\n"
      ) ;

      printf(
     "\n"
     " -autoweight = Compute a weight function using the 3dAutomask\n"
     "               algorithm plus some blurring of the base image.\n"
     "       **N.B.: '-autoweight+100' means to zero out all voxels\n"
     "                 with values below 100 before computing the weight.\n"
     "               '-autoweight**1.5' means to compute the autoweight\n"
     "                 and then raise it to the 1.5-th power (e.g., to\n"
     "                 increase the weight of high-intensity regions).\n"
     "               These two processing steps can be combined, as in\n"
     "                 '-autoweight+100**1.5'\n"
     "               ** Note that '**' must be enclosed in quotes;\n"
     "                  otherwise, the shell will treat it as a wildcard\n"
     "                  and you will get an error message before 3dAllineate\n"
     "                  even starts!!\n"
     "               ** UPDATE: one can now use '^' for power notation, to \n"
     "                  avoid needing to enclose the string in quotes.\n"
      ) ;
      if( visible_noweights ){
         printf(
     "       **N.B.: Some cost functionals do not allow -autoweight, and\n"
     "               will use -automask instead.  A warning message\n"
     "               will be printed if you run into this situation.\n"
     "               If a clip level '+xxx' is appended to '-autoweight',\n"
     "               then the conversion into '-automask' will NOT happen.\n"
     "               Thus, using a small positive '+xxx' can be used trick\n"
     "               -autoweight into working on any cost functional.\n"
         ) ;
      }
      printf(
     "\n"
     " -automask   = Compute a mask function, which is like -autoweight,\n"
     "               but the weight for a voxel is set to either 0 or 1.\n"
     "       **N.B.: '-automask+3' means to compute the mask function, and\n"
     "               then dilate it outwards by 3 voxels (e.g.).\n"
     "               ** Note that '+' means something very different\n"
     "                  for '-automask' and '-autoweight'!!\n"
     "\n"
     " -autobox    = Expand the -automask function to enclose a rectangular\n"
     "               box that holds the irregular mask.\n"
     "       **N.B.: This is the default mode of operation!\n"
     "               For intra-modality registration, '-autoweight' may be better!\n"
     "             * If the cost functional is 'ls', then '-autoweight' will be\n"
     "               the default, instead of '-autobox'.\n"
     "\n"
     " -nomask     = Don't compute the autoweight/mask; if -weight is not\n"
     "               also used, then every voxel will be counted equally.\n"
     "\n"
     " -weight www = Set the weighting for each voxel in the base dataset;\n"
     "               larger weights mean that voxel counts more in the cost\n"
     "               function.\n"
     "       **N.B.: The weight dataset must be defined on the same grid as\n"
     "               the base dataset.\n"
     "       **N.B.: Even if a method does not allow -autoweight, you CAN\n"
     "               use a weight dataset that is not 0/1 valued.  The\n"
     "               risk is yours, of course (!*! as always in AFNI !*!).\n"
     "\n"
     " -wtprefix p = Write the weight volume to disk as a dataset with\n"
     "               prefix name 'p'.  Used with '-autoweight/mask', this option\n"
     "               lets you see what voxels were important in the algorithm.\n"
     "\n"
     " -emask ee   = This option lets you specify a mask of voxels to EXCLUDE from\n"
     "               the analysis. The voxels where the dataset 'ee' is nonzero\n"
     "               will not be included (i.e., their weights will be set to zero).\n"
     "             * Like all the weight options, it applies in the base image\n"
     "               coordinate system.\n"
     "            ** Like all the weight options, it means nothing if you are using\n"
     "               one of the 'apply' options.\n"
      ) ;

      if( visible_noweights > 0 ){
        printf("\n"
               "    Method  Allows -autoweight\n"
               "    ------  ------------------\n") ;
        for( ii=0 ; ii < NMETH ; ii++ )
          if( meth_visible[ii] )
            printf("     %-4s   %s\n", meth_shortname[ii] ,
                                       meth_noweight[ii] ? "NO" : "YES" ) ;
      }

      printf(
       "\n"
       " -source_mask sss = Mask the source (input) dataset, using 'sss'.\n"
       " -source_automask = Automatically mask the source dataset.\n"
       "                      [By default, all voxels in the source]\n"
       "                      [dataset are used in the matching.   ]\n"
       "            **N.B.: You can also use '-source_automask+3' to dilate\n"
       "                    the default source automask outward by 3 voxels.\n"
      ) ;

      printf(
       "\n"
       " -warp xxx   = Set the warp type to 'xxx', which is one of\n"
       "                 shift_only         *OR* sho =  3 parameters\n"
       "                 shift_rotate       *OR* shr =  6 parameters\n"
       "                 shift_rotate_scale *OR* srs =  9 parameters\n"
       "                 affine_general     *OR* aff = 12 parameters\n"
       "               [Default = affine_general, which includes image]\n"
       "               [      shifts, rotations, scaling, and shearing]\n"
       "             * MY OPINION: Shearing is usually unimportant, so\n"
       "                            you can omit it if you want: '-warp srs'.\n"
       "                           But it doesn't hurt to keep shearing,\n"
       "                            except for a little extra CPU time.\n"
       "                           On the other hand, scaling is often\n"
       "                            important, so should not be omitted.\n"
       "\n"
       " -warpfreeze = Freeze the non-rigid body parameters (those past #6)\n"
       "               after doing the first sub-brick.  Subsequent volumes\n"
       "               will have the same spatial distortions as sub-brick #0,\n"
       "               plus rigid body motions only.\n"
       "             * MY OPINION: This option is almost useless.\n"
       "\n"
       " -replacebase   = If the source has more than one sub-brick, and this\n"
       "                  option is turned on, then after the #0 sub-brick is\n"
       "                  aligned to the base, the aligned #0 sub-brick is used\n"
       "                  as the base image for subsequent source sub-bricks.\n"
       "                * MY OPINION: This option is almost useless.\n"
       "\n"
       " -replacemeth m = After sub-brick #0 is aligned, switch to method 'm'\n"
       "                  for later sub-bricks.  For use with '-replacebase'.\n"
       "                * MY OPINION: This option is almost useless.\n"
       "\n"
       " -EPI        = Treat the source dataset as being composed of warped\n"
       "               EPI slices, and the base as comprising anatomically\n"
       "               'true' images.  Only phase-encoding direction image\n"
       "               shearing and scaling will be allowed with this option.\n"
       "       **N.B.: For most people, the base dataset will be a 3dSkullStrip-ed\n"
       "               T1-weighted anatomy (MPRAGE or SPGR).  If you don't remove\n"
       "               the skull first, the EPI images (which have little skull\n"
       "               visible due to fat-suppression) might expand to fit EPI\n"
       "               brain over T1-weighted skull.\n"
       "       **N.B.: Usually, EPI datasets don't have as complete slice coverage\n"
       "               of the brain as do T1-weighted datasets.  If you don't use\n"
       "               some option (like '-EPI') to suppress scaling in the slice-\n"
       "               direction, the EPI dataset is likely to stretch the slice\n"
       "               thickness to better 'match' the T1-weighted brain coverage.\n"
#if 0
       "       **N.B.: '-EPI' turns on '-warpfreeze -replacebase -replacemeth ls'.\n"
       "               To disable '-replacemeth ls', use '-replacemeth 0' after '-EPI'.\n"
#else
       "       **N.B.: '-EPI' turns on '-warpfreeze -replacebase'.\n"
#endif
       "               You can use '-nowarpfreeze' and/or '-noreplacebase' AFTER the\n"
       "               '-EPI' on the command line if you do not want these options used.\n"
       "\n"
       "  ** OPTIONS to change search ranges for alignment parameters **\n"
       "\n"
       " -smallrange   = Set all the parameter ranges to be smaller (about half) than\n"
       "                 the default ranges, which are rather large for many purposes.\n"
       "                * Default angle range    is plus/minus 30 degrees\n"
       "                * Default shift range    is plus/minus 32%% of grid size\n"
       "                * Default scaling range  is plus/minus 20%% of grid size\n"
       "                * Default shearing range is plus/minus 0.1111\n"
       "\n"
       " -parfix n v   = Fix parameter #n to be exactly at value 'v'.\n"
     "\n"
       " -parang n b t = Allow parameter #n to range only between 'b' and 't'.\n"
       "                 If not given, default ranges are used.\n"
     "\n"
       " -parini n v   = Initialize parameter #n to value 'v', but then\n"
       "                 allow the algorithm to adjust it.\n"
       "         **N.B.: Multiple '-par...' options can be used, to constrain\n"
       "                 multiple parameters.\n"
       "         **N.B.: -parini has no effect if -twopass is used, since\n"
       "                 the -twopass algorithm carries out its own search\n"
       "                 for initial parameters.\n"
       "\n"
       " -maxrot dd    = Allow maximum rotation of 'dd' degrees.  Equivalent\n"
       "                 to '-parang 4 -dd dd -parang 5 -dd dd -parang 6 -dd dd'\n"
       "                 [Default=30 degrees]\n"
     "\n"
       " -maxshf dd    = Allow maximum shift of 'dd' millimeters.  Equivalent\n"
       "                 to '-parang 1 -dd dd -parang 2 -dd dd -parang 3 -dd dd'\n"
       "                 [Default=32%% of the size of the base image]\n"
       "         **N.B.: This max shift setting is relative to the center-of-mass\n"
       "                 shift, if the '-cmass' option is used.\n"
     "\n"
       " -maxscl dd    = Allow maximum scaling factor to be 'dd'.  Equivalent\n"
       "                 to '-parang 7 1/dd dd -parang 8 1/dd dd -paran2 9 1/dd dd'\n"
       "                 [Default=1.2=image can go up or down 20%% in size]\n"
     "\n"
       " -maxshr dd    = Allow maximum shearing factor to be 'dd'. Equivalent\n"
       "                 to '-parang 10 -dd dd -parang 11 -dd dd -parang 12 -dd dd'\n"
       "                 [Default=0.1111 for no good reason]\n"
       "\n"
       " NOTE: If the datasets being registered have only 1 slice, 3dAllineate\n"
       "       will automatically fix the 6 out-of-plane motion parameters to\n"
       "       their 'do nothing' values, so you don't have to specify '-parfix'.\n"
#if 0
       "\n"
       " -matini mmm   = Initialize 3x4 affine transformation matrix to 'mmm',\n"
       "                 which is either a .1D file or an expression in the\n"
       "                 syntax of program 1dmatcalc.  Using this option is\n"
       "                 like using '-parini' on all affine matrix parameters.\n"
#endif
       "\n"
       " -master mmm = Write the output dataset on the same grid as dataset\n"
       "               'mmm'.  If this option is NOT given, the base dataset\n"
       "               is the master.\n"
       "       **N.B.: 3dAllineate transforms the source dataset to be 'similar'\n"
       "               to the base image.  Therefore, the coordinate system\n"
       "               of the master dataset is interpreted as being in the\n"
       "               reference system of the base image.  It is thus vital\n"
       "               that these finite 3D volumes overlap, or you will lose data!\n"
       "       **N.B.: If 'mmm' is the string 'SOURCE', then the source dataset\n"
       "               is used as the master for the output dataset grid.\n"
       "               You can also use 'BASE', which is of course the default.\n"
       "\n"
       " -mast_dxyz del = Write the output dataset using grid spacings of\n"
       "  *OR*            'del' mm.  If this option is NOT given, then the\n"
       " -newgrid del     grid spacings in the master dataset will be used.\n"
       "                  This option is useful when registering low resolution\n"
       "                  data (e.g., EPI time series) to high resolution\n"
       "                  datasets (e.g., MPRAGE) where you don't want to\n"
       "                  consume vast amounts of disk space interpolating\n"
       "                  the low resolution data to some artificially fine\n"
       "                  (and meaningless) spatial grid.\n"
     ) ;

     printf(
      "\n"
      "----------------------------------------------\n"
      "DEFINITION OF AFFINE TRANSFORMATION PARAMETERS\n"
      "----------------------------------------------\n"
      "The 3x3 spatial transformation matrix is calculated as [S][D][U],\n"
      "where [S] is the shear matrix,\n"
      "      [D] is the scaling matrix, and\n"
      "      [U] is the rotation (proper orthogonal) matrix.\n"
      "Thes matrices are specified in DICOM-ordered (x=-R+L,y=-A+P,z=-I+S)\n"
      "coordinates as:\n"
      "\n"
      "  [U] = [Rotate_y(param#6)] [Rotate_x(param#5)] [Rotate_z(param #4)]\n"
      "        (angles are in degrees)\n"
      "\n"
      "  [D] = diag( param#7 , param#8 , param#9 )\n"
      "\n"
      "        [    1        0     0 ]        [ 1 param#10 param#11 ]\n"
      "  [S] = [ param#10    1     0 ]   OR   [ 0    1     param#12 ]\n"
      "        [ param#11 param#12 1 ]        [ 0    0        1     ]\n"
      "\n"
      "The shift vector comprises parameters #1, #2, and #3.\n"
      "\n"
      "The goal of the program is to find the warp parameters such that\n"
      "   I([x]_warped) 'is similar to' J([x]_in)\n"
      "as closely as possible in some sense of 'similar', where J(x) is the\n"
      "base image, and I(x) is the source image.\n"
      "\n"
      "Using '-parfix', you can specify that some of these parameters\n"
      "are fixed.  For example, '-shift_rotate_scale' is equivalent\n"
      "'-affine_general -parfix 10 0 -parfix 11 0 -parfix 12 0'.\n"
      "Don't even think of using the '-parfix' option unless you grok\n"
      "this example!\n"
      "\n"
      "----------- Special Note for the '-EPI' Option's Coordinates -----------\n"
      "In this case, the parameters above are with reference to coordinates\n"
      "  x = frequency encoding direction (by default, first axis of dataset)\n"
      "  y = phase encoding direction     (by default, second axis of dataset)\n"
      "  z = slice encoding direction     (by default, third axis of dataset)\n"
      "This option lets you freeze some of the warping parameters in ways that\n"
      "make physical sense, considering how echo-planar images are acquired.\n"
      "The x- and z-scaling parameters are disabled, and shears will only affect\n"
      "the y-axis.  Thus, there will be only 9 free parameters when '-EPI' is\n"
      "used.  If desired, you can use a '-parang' option to allow the scaling\n"
      "fixed parameters to vary (put these after the '-EPI' option):\n"
      "  -parang 7 0.833 1.20     to allow x-scaling\n"
      "  -parang 9 0.833 1.20     to allow z-scaling\n"
      "You could also fix some of the other parameters, if that makes sense\n"
      "in your situation; for example, to disable out-of-slice rotations:\n"
      "  -parfix 5 0  -parfix 6 0\n"
      "and to disable out of slice translation:\n"
      "  -parfix 3 0\n"
      "NOTE WELL: If you use '-EPI', then the output warp parameters (e.g., in\n"
      "           '-1Dparam_save') apply to the (freq,phase,slice) xyz coordinates,\n"
      "           NOT to the DICOM xyz coordinates, so equivalent transformations\n"
      "           will be expressed with different sets of parameters entirely\n"
      "           than if you don't use '-EPI'!  This comment does NOT apply\n"
      "           to the output of '-1Dmatrix_save', since that matrix is\n"
      "           defined relative to the RAI (DICOM) spatial coordinates.\n"
     ) ;

     printf(
      "\n"
      "*********** CHANGING THE ORDER OF MATRIX APPLICATION ***********\n"
      "   {{{ There is no good reason to ever use these options! }}}\n"
      "\n"
      "  -SDU or -SUD }= Set the order of the matrix multiplication\n"
      "  -DSU or -DUS }= for the affine transformations:\n"
      "  -USD or -UDS }=   S = triangular shear (params #10-12)\n"
      "                    D = diagonal scaling matrix (params #7-9)\n"
      "                    U = rotation matrix (params #4-6)\n"
      "                  Default order is '-SDU', which means that\n"
      "                  the U matrix is applied first, then the\n"
      "                  D matrix, then the S matrix.\n"
      "\n"
      "  -Supper      }= Set the S matrix to be upper or lower\n"
      "  -Slower      }= triangular [Default=lower triangular]\n"
      "                  NOTE: There is no '-Lunch' option.\n"
      "                        There is no '-Faster' option.\n"
      "\n"
      "  -ashift OR   }= Apply the shift parameters (#1-3) after OR\n"
      "  -bshift      }= before the matrix transformation. [Default=after]\n"
     ) ;

     printf(
      "\n"
      "            ==================================================\n"
      "        ===== RWCox - September 2006 - Live Long and Prosper =====\n"
      "            ==================================================\n"
      "\n"
      "         ********************************************************\n"
      "        *** From Webster's Dictionary: Allineate == 'to align' ***\n"
      "         ********************************************************\n"
     ) ;

     /*......................................................................*/

     if( 1 ){   /* this used to be only for "-HELP" */
       printf(
        "\n"
        "===========================================================================\n"
        "                       FORMERLY SECRET HIDDEN OPTIONS\n"
        "---------------------------------------------------------------------------\n"
        "        ** N.B.: Most of these are experimental! [permanent beta] **\n"
        "===========================================================================\n"
        "\n"
        " -num_rtb n  = At the beginning of the fine pass, the best set of results\n"
        "               from the coarse pass are 'refined' a little by further\n"
        "               optimization, before the single best one is chosen for\n"
        "               for the final fine optimization.\n"
        "              * This option sets the maximum number of cost functional\n"
        "                evaluations to be used (for each set of parameters)\n"
        "                in this step.\n"
        "              * The default is 99; a larger value will take more CPU\n"
        "                time but may give more robust results.\n"
        "              * If you want to skip this step entirely, use '-num_rtb 0'.\n"
        "                then, the best of the coarse pass results is taken\n"
        "                straight to the final optimization passes.\n"
        "       **N.B.: If you use '-VERB', you will see that one extra case\n"
        "               is involved in this initial fine refinement step; that\n"
        "               case is starting with the identity transformation, which\n"
        "               helps insure against the chance that the coarse pass\n"
        "               optimizations ran totally amok.\n"
        "             * MY OPINION: This option is mostly useless - but not always!\n"
        "                         * Every step in the multi-step alignment process\n"
        "                            was added at some point to solve a difficult\n"
        "                            alignment problem.\n"
        "                         * Since you usually don't know if YOUR problem\n"
        "                            is difficult, you should not reduce the default\n"
        "                            process without good reason.\n"
        "\n"
        " -nocast     = By default, parameter vectors that are too close to the\n"
        "               best one are cast out at the end of the coarse pass\n"
        "               refinement process. Use this option if you want to keep\n"
        "               them all for the fine resolution pass.\n"
        "             * MY OPINION: This option is nearly useless.\n"
        "\n"
        " -norefinal  = Do NOT re-start the fine iteration step after it\n"
        "               has converged.  The default is to re-start it, which\n"
        "               usually results in a small improvement to the result\n"
        "               (at the cost of CPU time).  This re-start step is an\n"
        "               an attempt to avoid a local minimum trap.  It is usually\n"
        "               not necessary, but sometimes helps.\n"
        "\n"
        " -realaxes   = Use the 'real' axes stored in the dataset headers, if they\n"
        "               conflict with the default axes.  [For Jedi AFNI Masters only!]\n"
        "\n"
        " -savehist sss = Save start and final 2D histograms as PGM\n"
        "                 files, with prefix 'sss' (cost: cr mi nmi hel).\n"
        "                * if filename contains 'FF', floats is written\n"
        "                * these are the weighted histograms!\n"
        "                * -savehist will also save histogram files when\n"
        "                  the -allcost evaluations takes place\n"
        "                * this option is mostly useless unless '-histbin' is\n"
        "                  also used\n"
        "               * MY OPINION: This option is mostly for debugging.\n"
#if 0
        " -seed iii     = Set random number seed (for coarse startup search)\n"
        "                 to 'iii'.\n"
        "                 [Default==7654321; if iii==0, a unique value is used]\n"
#endif
        " -median       = Smooth with median filter instead of Gaussian blur.\n"
        "                 (Somewhat slower, and not obviously useful.)\n"
        "               * MY OPINION: This option is nearly useless.\n"
        "\n"
        " -powell m a   = Set the Powell NEWUOA dimensional parameters to\n"
        "                 'm' and 'a' (cf. source code in powell_int.c).\n"
        "                 The number of points used for approximating the\n"
        "                 cost functional is m*N+a, where N is the number\n"
        "                 of parameters being optimized.  The default values\n"
        "                 are m=2 and a=3.  Larger values will probably slow\n"
        "                 the program down for no good reason.  The smallest\n"
        "                 allowed values are 1.\n"
        "               * MY OPINION: This option is nearly useless.\n"
        "\n"
        " -target ttt   = Same as '-source ttt'.  In the earliest versions,\n"
        "                 what I now call the 'source' dataset was called the\n"
        "                 'target' dataset:\n"
        "                    Try to remember the kind of September (2006)\n"
        "                    When life was slow and oh so mellow\n"
        "                    Try to remember the kind of September\n"
        "                    When grass was green and source was target.\n"
#if 0
        " -targijk      = Align source xyz axes with ijk indexes, rather than\n"
        "                 using coordinates in target header.\n"
#endif
        " -Xwarp       =} Change the warp/matrix setup so that only the x-, y-, or z-\n"
        " -Ywarp       =} axis is stretched & sheared.  Useful for EPI, where 'X',\n"
        " -Zwarp       =} 'Y', or 'Z' corresponds to the phase encoding direction.\n"
        " -FPS fps      = Generalizes -EPI to arbitrary permutation of directions.\n"
        "\n"
        " -histpow pp   = By default, the number of bins in the histogram used\n"
        "                 for calculating the Hellinger, Mutual Information, and\n"
        "                 Correlation Ratio statistics is n^(1/3), where n is\n"
        "                 the number of data points.  You can change that exponent\n"
        "                 to 'pp' with this option.\n"
        " -histbin nn   = Or you can just set the number of bins directly to 'nn'.\n"
        " -eqbin   nn   = Use equalized marginal histograms with 'nn' bins.\n"
        " -clbin   nn   = Use 'nn' equal-spaced bins except for the bot and top,\n"
        "                 which will be clipped (thus the 'cl').  If nn is 0, the\n"
        "                 program will pick the number of bins for you.\n"
        "                 **N.B.: '-clbin 0' is now the default [25 Jul 2007];\n"
        "                         if you want the old all-equal-spaced bins, use\n"
        "                         '-histbin 0'.\n"
        "                 **N.B.: '-clbin' only works when the datasets are\n"
        "                         non-negative; any negative voxels in either\n"
        "                         the input or source volumes will force a switch\n"
        "                         to all equal-spaced bins.\n"
        "               * MY OPINION: The above histogram-altering options are useless.\n"
        "\n"
        " -wtmrad  mm   = Set autoweight/mask median filter radius to 'mm' voxels.\n"
        " -wtgrad  gg   = Set autoweight/mask Gaussian filter radius to 'gg' voxels.\n"
        " -nmsetup nn   = Use 'nn' points for the setup matching [default=98756]\n"
        " -ignout       = Ignore voxels outside the warped source dataset.\n"
        "\n"
        " -blok bbb     = Blok definition for the 'lp?' (Local Pearson) cost\n"
        "                 functions: 'bbb' is one of\n"
        "                   'BALL(r)' or 'CUBE(r)' or 'RHDD(r)' or 'TOHD(r)'\n"
        "                 corresponding to\n"
        "                   spheres or cubes or rhombic dodecahedra or\n"
        "                   truncated octahedra\n"
        "                 where 'r' is the size parameter in mm.\n"
        "                 [Default is 'RHDD(6.54321)' (rhombic dodecahedron)]\n"
        "               * Changing the 'blok' definition/radius should only be\n"
        "                 needed if the resolution of the base dataset is\n"
        "                 radically different from the common 1 mm.\n"
        "               * HISTORICAL NOTES:\n"
        "                 * CUBE, RHDD, and TOHD are space filling polyhedra.\n"
        "                 * To even approximately fill space, BALLs must overlap,\n"
        "                   unlike the other blok shapes. Which means that BALL\n"
        "                   bloks will use some voxels twice.\n"
        "                 * The TOHD is the 'most compact' or 'most ball-like'\n"
        "                   of the known convex space filling polyhedra.\n"
        "                 * Kepler discovered/invented the RHDD (honeybees also did).\n"
        "\n"
        " -PearSave sss = Save the final local Pearson correlations into a dataset\n"
        "   *OR*          with prefix 'sss'. These are the correlations from\n"
        " -SavePear sss   which the lpc and lpa cost functionals are calculated.\n"
        "                * The values will be between -1 and 1 in each blok.\n"
        "                   See the 'Too Much Detail' section below for how\n"
        "                   these correlations are used to compute lpc and lpa.\n"
        "                * Locations not used in the matching will get 0.\n"
        "               ** Unless you use '-nmatch 100%%', there will be holes\n"
        "                   of 0s in the bloks, as not all voxels are used in\n"
        "                   the matching algorithm (speedup attempt).\n"
        "                * All the matching points in a given blok will get\n"
        "                   the same value, which makes the resulting dataset\n"
        "                   look jauntily blocky, especially in color.\n"
        "                * This saved dataset will be on the grid of the base\n"
        "                   dataset, and may be zero padded if the program\n"
        "                   chose to do so in it wisdom. This padding means\n"
        "                   that the voxels in this output dataset may not\n"
        "                   match one-to-one with the voxels in the base\n"
        "                   dataset; however, AFNI displays things using\n"
        "                   coordinates, so overlaying this dataset on the\n"
        "                   base dataset (say) should work OK.\n"
        "                * If you really want this saved dataset to be on the\n"
        "                   grid as the base dataset, you'll have use\n"
        "                     3dZeropad -master {Base Dataset} ....\n"
        "                * Option '-PearSave' works even if you don't use the\n"
        "                   'lpc' or 'lpa' cost functionals.\n"
        "                * If you use this option combined with '-allcostX', then\n"
        "                   the local correlations will be saved from the INITIAL\n"
        "                   alignment parameters, rather than from the FINAL\n"
        "                   optimized parameters.\n"
        "                   (Of course, with '-allcostX', there IS no final result.)\n"
        "                * This option does NOT work with '-allcost' or '-allcostX1D'.\n"
        "\n"
        " -allcost        = Compute ALL available cost functionals and print them\n"
        "                   at various points in the optimization progress.\n"
        " -allcostX       = Compute and print ALL available cost functionals for the\n"
        "                   un-warped inputs, and then quit.\n"
        "                  * This option is for testing purposes (AKA 'fun').\n"
        " -allcostX1D p q = Compute ALL available cost functionals for the set of\n"
        "                   parameters given in the 1D file 'p' (12 values per row),\n"
        "                   write them to the 1D file 'q', then exit. (For you, Zman)\n"
        "                  * N.B.: If -fineblur is used, that amount of smoothing\n"
        "                          will be applied prior to the -allcostX evaluations.\n"
        "                          The parameters are the rotation, shift, scale,\n"
        "                          and shear values, not the affine transformation\n"
        "                          matrix. An identity matrix could be provided as\n"
        "                          \"0 0 0  0 0 0  1 1 1  0 0 0\" for instance or by\n"
        "                          using the word \"IDENTITY\"\n"
        "                  * This option is for testing purposes (even more 'fun').\n"
       ) ;

       printf("\n"
        "===========================================================================\n" );
       printf("\n"
        "Too Much Detail -- How Local Pearson Correlations Are Computed and Used\n"
        "-----------------------------------------------------------------------\n"
        " * The automask region of the base dataset is divided into a discrete\n"
        "    set of 'bloks'. Usually there are several thousand bloks.\n"
        " * In each blok, the voxel values from the base and the source (after\n"
        "    the alignment transformation is applied) are extracted and the\n"
        "    correlation coefficient is computed -- either weighted or unweighted,\n"
        "    depending on the options used in 3dAllineate (usually weighted).\n"
        " * Let p[i] = correlation coefficient in blok #i,\n"
        "       w[i] = sum of weights used in blok #i, or = 1 if unweighted.\n"
        "** The values of p[i] are what get output via the '-PearSave' option.\n"
        " * Define pc[i] = arctanh(p[i]) = 0.5 * log( (1+p[i]) / (1-p[i]) )\n"
        "     This expression is designed to 'stretch' out larger correlations,\n"
        "     giving them more emphasis in psum below. The same reasoning\n"
        "     is why pc[i]*abs(pc[i]) is used below, to make bigger correlations\n"
        "     have a bigger impact in the final result.\n"
        " * psum = SUM_OVER_i { w[i]*pc[i]*abs(pc[i]) }\n"
        "   wsum = SUM_OVER_i { w[i] }\n"
        "   lpc  = psum / wsum   ==> negative correlations are good (smaller lpc)\n"
        "   lpa  = 1 - abs(lpc)  ==> positive correlations are good (smaller lpa)\n"
       ) ;

       printf("\n"
        "===========================================================================\n" );
       printf("\n"
        "Modifying '-final wsinc5' -- for the truly crazy people out there\n"
        "-----------------------------------------------------------------\n"
        " * The windowed (tapered) sinc function interpolation can be modified\n"
        "     by several environment variables.  This is expert-level stuff, and\n"
        "     you should understand what you are doing if you use these options.\n"
        "     The simplest way to use these would be on the command line, as in\n"
        "       -DAFNI_WSINC5_RADIUS=9 -DAFNI_WSINC5_TAPERFUN=Hamming\n"
        "\n"
        " * AFNI_WSINC5_TAPERFUN lets you choose the taper function.\n"
        "     The default taper function is the minimum sidelobe 3-term cosine:\n"
        "       0.4243801 + 0.4973406*cos(PI*x) + 0.0782793*cos(2*PI*x)\n"
        "     If you set this environment variable to 'Hamming', then the\n"
        "     minimum sidelobe 2-term cosine will be used instead:\n"
        "       0.53836 + 0.46164*cos(PI*x)\n"
        "     Here, 'x' is between 0 and 1, where x=0 is the center of the\n"
        "     interpolation mask and x=1 is the outer edge.\n"
        " ++  Unfortunately, the 3-term cosine doesn't have a catchy name; you can\n"
        "       find it (and many other) taper functions described in the paper\n"
        "         AH Nuttall, Some Windows with Very Good Sidelobe Behavior.\n"
        "         IEEE Trans. ASSP, 29:84-91 (1981).\n"
        "       In particular, see Fig.14 and Eq.36 in this paper.\n"
        "\n"
        " * AFNI_WSINC5_TAPERCUT lets you choose the start 'x' point for tapering:\n"
        "     This value should be between 0 and 0.8; for example, 0 means to taper\n"
        "     all the way from x=0 to x=1 (maximum tapering).  The default value\n"
        "     is 0.  Setting TAPERCUT to 0.5 (say) means only to taper from x=0.5\n"
        "     to x=1; thus, a larger value means that fewer points are tapered\n"
        "     inside the interpolation mask.\n"
        "\n"
        " * AFNI_WSINC5_RADIUS lets you choose the radius of the tapering window\n"
        "     (i.e., the interpolation mask region).  This value is an integer\n"
        "     between 3 and 21.  The default value is 5 (which used to be the\n"
        "     ONLY value, thus 'wsinc5').  RADIUS is measured in voxels, not mm.\n"
        "\n"
        " * AFNI_WSINC5_SPHERICAL lets you choose the shape of the mask region.\n"
        "     If you set this value to 'Yes', then the interpolation mask will be\n"
        "     spherical; otherwise, it defaults to cubical.\n"
        "\n"
        " * The Hamming taper function is a little faster than the 3-term function,\n"
        "     but will have a little more Gibbs phenomenon.\n"
        " * A larger TAPERCUT will give a little more Gibbs phenomenon; compute\n"
        "     speed won't change much with this parameter.\n"
        " * Compute time goes up with (at least) the 3rd power of the RADIUS; setting\n"
        "     RADIUS to 21 will be VERY slow.\n"
        " * Visually, RADIUS=3 is similar to quintic interpolation.  Increasing\n"
        "     RADIUS makes the interpolated images look sharper and more well-\n"
        "     defined.  However, values of RADIUS greater than or equal to 7 appear\n"
        "     (to Zhark's eagle eye) to be almost identical.  If you really care,\n"
        "     you'll have to experiment with this parameter yourself.\n"
        " * A spherical mask is also VERY slow, since the cubical mask allows\n"
        "     evaluation as a tensor product.  There is really no good reason\n"
        "     to use a spherical mask; I only put it in for fun/experimental purposes.\n"
        "** For most users, there is NO reason to ever use these environment variables\n"
        "     to modify wsinc5.  You should only do this kind of thing if you have a\n"
        "     good and articulable reason!  (Or if you really like to screw around.)\n"
        "** The wsinc5 interpolation function is parallelized using OpenMP, which\n"
        "     makes its usage moderately tolerable.\n"
#ifndef USE_OMP
        "   ++ However, this binary copy of AFNI is NOT compiled with OpenMP support.\n"
        "        You should consider getting such binaries, as several AFNI program\n"
        "        (including this one) will become significantly faster.\n"
#endif
       ) ;

       printf("\n"
        "===========================================================================\n" );
       printf("\n"
              "Hidden experimental cost functionals:\n"
              "-------------------------------------\n" ) ;
       for( ii=0 ; ii < NMETH ; ii++ )
        if( !meth_visible[ii] )
          printf( "   %-4s *OR*  %-16s= %s\n" ,
                  meth_shortname[ii] , meth_longname[ii] , meth_username[ii] );

       printf("\n"
              "Notes for the new [Feb 2010] lpc+ cost functional:\n"
              "--------------------------------------------------\n"
              " * The cost functional named 'lpc+' is a combination of several others:\n"
              "     lpc + hel*%.1f + crA*%.1f + nmi*%.1f + mi*%.1f + ov*%.1f\n"
              "   ++ 'hel', 'crA', 'nmi', and 'mi' are the histogram-based cost\n"
              "      functionals also available as standalone options.\n"
              "   ++ 'ov' is a measure of the overlap of the automasks of the base and\n"
              "      source volumes; ov is not available as a standalone option.\n"
              "\n"
              " * The purpose of lpc+ is to avoid situations where the pure lpc cost\n"
              "   goes wild; this especially happens if '-source_automask' isn't used.\n"
              "   ++ Even with lpc+, you should use '-source_automask+2' (say) to be safe.\n"
              "\n"
              " * You can alter the weighting of the extra functionals by giving the\n"
              "   option in the form (for example)\n"
              "     '-lpc+hel*0.5+nmi*0+mi*0+crA*1.0+ov*0.5'\n"
              "\n"
              " * The quotes are needed to prevent the shell from wild-card expanding\n"
              "   the '*' character.\n"
              "   --> You can now use ':' in place of '*' to avoid this wildcard problem:\n"
              "         -lpc+hel:0.5+nmi:0+mi:0+crA:1+ov:0.5+ZZ\n"
              "\n"
              " * Notice the weight factors FOLLOW the name of the extra functionals.\n"
              "   ++ If you want a weight to be 0 or 1, you have to provide for that\n"
              "      explicitly -- if you leave a weight off, then it will get its\n"
              "      default value!\n"
              "   ++ The order of the weight factor names is unimportant here:\n"
              "        '-lpc+hel*0.5+nmi*0.8' == '-lpc+nmi*0.8+hel*0.5'\n"
              "\n"
              " * Only the 5 functionals listed (hel,crA,nmi,mi,ov) can be used in '-lpc+'.\n"
              "\n"
              " * In addition, if you want the initial alignments to be with '-lpc+' and\n"
              "   then finish the Final alignment with pure '-lpc', you can indicate this\n"
              "   by putting 'ZZ' somewhere in the option string, as in '-lpc+ZZ'.\n"
              " ***** '-cost lpc+ZZ' is very useful for aligning EPI to T1w volumes *****\n"
              "\n"
              " * [28 Nov 2018]\n"
              "   All of the above now applies to the 'lpa+' cost functional,\n"
              "   which can be used as a robust method for like-to-like alignment.\n"
             , DEFAULT_MICHO_HEL , DEFAULT_MICHO_CRA , DEFAULT_MICHO_NMI , DEFAULT_MICHO_MI , DEFAULT_MICHO_OV
             ) ;

       printf("\n"
              "Cost functional descriptions (for use with -allcost output):\n"
              "------------------------------------------------------------\n"
             ) ;
       for( ii=0 ; ii < NMETH ; ii++ )
         printf("   %-4s:: %s\n",
                meth_shortname[ii] , meth_costfunctional[ii] ) ;

       printf("\n") ;
       printf(" * N.B.: Some cost functional values (as printed out above)\n"
              "   are negated from their theoretical descriptions (e.g., 'hel')\n"
              "   so that the best image alignment will be found when the cost\n"
              "   is minimized.  See the descriptions above and the references\n"
              "   below for more details for each functional.\n");
       printf("\n") ;
       printf(" * MY OPINIONS:\n"
              "   * Some of these cost functionals were implemented only for\n"
              "      the purposes of fun and/or comparison and/or experimentation\n"
              "      and/or special circumstances. These are\n"
              "        sp je lss crM crA crM hel mi nmi\n"
              "   * For many purposes, lpc+ZZ and lpa+ZZ are the most robust\n"
              "      cost functionals, but usually the slowest to evaluate.\n"
              "   * HOWEVER, just because some method is best MOST of the\n"
              "      time does not mean it is best ALL of the time.\n"
              "      Please check your results visually, or at some point\n"
              "      in time you will have bad results and not know it!\n"
              "   * For speed and for 'like-to-like' alignment, '-cost ls'\n"
              "      can work well.\n") ;
       printf("\n") ;
       printf(" * For more information about the 'lpc' functional, see\n"
              "     ZS Saad, DR Glen, G Chen, MS Beauchamp, R Desai, RW Cox.\n"
              "       A new method for improving functional-to-structural\n"
              "       MRI alignment using local Pearson correlation.\n"
              "       NeuroImage 44: 839-848, 2009.\n"
              "     http://dx.doi.org/10.1016/j.neuroimage.2008.09.037\n"
              "     https://pubmed.ncbi.nlm.nih.gov/18976717\n"
              "   The '-blok' option can be used to control the regions\n"
              "   (size and shape) used to compute the local correlations.\n");
       printf(" *** Using the 'lpc' functional wisely requires the use of\n"
              "     a proper weight volume.  We HIGHLY recommend you use\n"
              "     the align_epi_anat.py script if you want to use this\n"
              "     cost functional!  Otherwise, you are likely to get\n"
              "     less than optimal results (and then swear at us unjustly).\n") ;
       printf("\n") ;
       printf(" * For more information about the 'cr' functionals, see\n"
              "     http://en.wikipedia.org/wiki/Correlation_ratio\n"
              "   Note that CR(x,y) is not the same as CR(y,x), which\n"
              "   is why there are symmetrized versions of it available.\n") ;
       printf("\n") ;
       printf(" * For more information about the 'mi', 'nmi', and 'je'\n"
              "   cost functionals, see\n"
              "     http://en.wikipedia.org/wiki/Mutual_information\n"
              "     http://en.wikipedia.org/wiki/Joint_entropy\n"
              "     http://www.cs.jhu.edu/~cis/cista/746/papers/mutual_info_survey.pdf\n");
       printf("\n") ;
       printf(" * For more information about the 'hel' functional, see\n"
              "     http://en.wikipedia.org/wiki/Hellinger_distance\n"     ) ;
       printf("\n") ;
       printf(" * Some cost functionals (e.g., 'mi', 'cr', 'hel') are\n"
              "   computed by creating a 2D joint histogram of the\n"
              "   base and source image pair.  Various options above\n"
              "   (e.g., '-histbin', etc.) can be used to control the\n"
              "   number of bins used in the histogram on each axis.\n"
              "   (If you care to control the program in such detail!)\n"  ) ;
       printf("\n") ;
       printf(" * Minimization of the chosen cost functional is done via\n"
              "   the NEWUOA software, described in detail in\n"
              "     MJD Powell. 'The NEWUOA software for unconstrained\n"
              "       optimization without derivatives.' In: GD Pillo,\n"
              "       M Roma (Eds), Large-Scale Nonlinear Optimization.\n"
              "       Springer, 2006.\n"
              "     http://www.damtp.cam.ac.uk/user/na/NA_papers/NA2004_08.pdf\n");

       printf("\n"
        "===========================================================================\n"
        "\n"
        "SUMMARY of the Default Allineation Process\n"
        "------------------------------------------\n"
        "As mentioned earlier, each of these steps was added to deal with a problem\n"
        " that came up over the years. The resulting process is reasonably robust :),\n"
        " but then tends to be slow :(. If you use the '-verb' or '-VERB' option, you\n"
        " will get a lot of fun fun fun progress messages that show the results from\n"
        " this sequence of steps.\n"
        "\n"
        "Below, I refer to different scales of effort in the optimizations at each\n"
        " step. Easier/faster optimization is done using: matching with fewer points\n"
        " from the datasets; more smoothing of the base and source datasets; and by\n"
        " putting a smaller upper limit on the number of trials the optimizer is\n"
        " allowed to take. The Coarse phase starts with the easiest optimization,\n"
        " and increases the difficulty a little at each refinement. The Fine phase\n"
        " starts with the most difficult optimization setup: the most points for\n"
        " matching, little or no smoothing, and a large limit on the number of\n"
        " optimizer trials.\n"
        "\n"
        " 0. Preliminary Setup [Goal: create the basis for the following steps]\n"
        "  a. Create the automask and/or autoweight from the '-base' dataset.\n"
        "     The cost functional will only be computed from voxels inside the\n"
        "     automask, and only a fraction of those voxels will actually be used\n"
        "     for evaluating the cost functional (unless '-nmatch 100%%' is used).\n"
        "  b. If the automask is 'too close' to the outside of the base 3D volume,\n"
        "     zeropad the base dataset to avoid edge effects.\n"
        "  c. Determine the 3D (x,y,z) shifts for the '-cmass' center-of-mass\n"
        "     crude alignment, if ordered by the user.\n"
        "  d. Set ranges of transformation parameters and which parameters are to\n"
        "     be frozen at fixed values.\n"
        "\n"
        " 1. Coarse Phase [Goal: explore the vastness of 6-12D parameter space]\n"
        "  a. The first step uses only the first 6 parameters (shifts + rotations),\n"
        "     and evaluates thousands of potential starting points -- selected from\n"
        "     a 6D grid in parameter space and also from random points in 6D\n"
        "     parameter space. This step is fairly slow. The best 45 parameter\n"
        "     sets (in the sense of the cost functional) are kept for the next step.\n"
        "  b. Still using only the first 6 parameters, the best 45 sets of parameters\n"
        "     undergo a little optimization. The best 6 parameter sets after this\n"
        "     refinement are kept for the next step. (The number of sets chosen\n"
        "     to go on to the next step can be set by the '-twobest' option.)\n"
        "     The optimizations in this step use the blurring radius that is\n"
        "     given by option '-twoblur', which defaults to 7.77 mm, and use\n"
        "     relatively few points in each dataset for computing the cost functional.\n"
        "  c. These 6 best parameter sets undergo further, more costly, optimization,\n"
        "     now using all 12 parameters. This optimization runs in 3 passes, each\n"
        "     more costly (less smoothing, more matching points) than the previous.\n"
        "     (If 2 sets get too close in parameter space, 1 of them will be cast out\n"
        "     -- this does not happen often.) Output parameter sets from the 3rd pass\n"
        "     of successive refinement are inputs to the fine refinement phase.\n"
        "\n"
        " 2. Fine Phase [Goal: use more expensive optimization on good starting points]\n"
        "  a. The 6 outputs from step 1c have the null parameter set (all 0, except\n"
        "     for the '-cmass' shifts) appended. Then a small amount of optimization\n"
        "     is applied to each of these 7 parameter sets ('-num_rtb'). The null\n"
        "     parameter set is added here to insure against the possibility that the\n"
        "     coarse optimizations 'ran away' to some unpleasant locations in the 12D\n"
        "     parameter space. These optimizations use the full set of points specified\n"
        "     by '-nmatch', and the smoothing specified by '-fineblur' (default = 0),\n"
        "     but the number of functional evaluations is small, to make this step fast.\n"
        "  b. The best (smallest cost) set from step 2a is chosen for the final\n"
        "     optimization, which is run until the '-conv' limit is reached.\n"
        "     These are the 'Finalish' parameters (shown using '-verb').\n"
        "  c. The set of parameters from step 2b is used as the starting point\n"
        "     for a new optimization, in an attempt to avoid a false minimum.\n"
        "     The results of this optimization are the final parameter set.\n"
        "\n"
        " 3. The final set of parameters is used to produce the output volume,\n"
        "    using the '-final' interpolation method.\n"
        "\n"
        "In practice, the output from the Coarse phase successive refinements is\n"
        "usually so good that the Fine phase runs quickly and makes only small\n"
        "adjustments. The quality resulting from the Coarse phase steps is mostly\n"
        "due, in my opinion, to the large number of initial trials (1ab), followed by\n"
        "by the successive refinements of several parameter sets (1c) to help usher\n"
        "'good' candidates to the starting line for the Fine phase.\n"
        "\n"
        "For some 'easy' registration problems -- such as T1w-to-T1w alignment, high\n"
        "quality images, a lot of overlap to start with -- the process can be sped\n"
        "up by reducing the number of steps. For example, '-num_rtb 0 -twobest 0'\n"
        "would eliminate step 2a and speed up step 1c. Even more extreme, '-onepass'\n"
        "could be used to skip all of the Coarse phase. But be careful out there!\n"
        "\n"
        "For 'hard' registration problems, cleverness is usually needed. Choice\n"
        "of cost functional matters. Preprocessing the datasets may be necessary.\n"
        "Using '-twobest %d' could help by providing more candidates for the\n"
        "Fine phase -- at the cost of CPU time. If you run into trouble -- which\n"
        "happens sooner or later -- try the AFNI Message Board -- and please\n"
        "give details, including the exact command line(s) you used.\n"
       , PARAM_MAXTRIAL
       ) ;

#if 0               /* No longer show help for -nwarp [29 Jan 2021] */
       printf("\n"
        "===========================================================================\n"
        "\n"
        " -nwarp type = Experimental nonlinear warping:\n"
        "\n"
        "              ***** Note that these '-nwarp' options are superseded   *****\n"
        "              ***** by the AFNI program 3dQwarp,  which does a more   *****\n"
        "              ***** accurate and more better job of nonlinear warping *****\n"
        "            ********* I strongly recommend against using -nwarp!!!  *********\n"
        "            ********* [And I will not support or help you with it.] *********\n"
        "            ******* ------ Zhark the Warper ------ July 2013 -------- *******\n"
        "\n"
        "              * At present, the only 'type' is 'bilinear',\n"
        "                as in 3dWarpDrive, with 39 parameters.\n"
        "              * I plan to implement more complicated nonlinear\n"
        "                warps in the future, someday .... [HAH!]\n"
        "              * -nwarp can only be applied to a source dataset\n"
        "                that has a single sub-brick!\n"
        "              * -1Dparam_save and -1Dparam_apply work with\n"
        "                bilinear warps; see the Notes for more information.\n"
        "        ==>>*** Nov 2010: I have now added the following polynomial\n"
        "                warps: 'cubic', 'quintic', 'heptic', 'nonic' (using\n"
        "                3rd, 5th, 7th, and 9th order Legendre polynomials); e.g.,\n"
        "                   -nwarp heptic\n"
        "              * These are the nonlinear warps that I now am supporting.\n"
        "              * Or you can call them 'poly3', 'poly5', 'poly7', and 'poly9',\n"
        "                  for simplicity and non-Hellenistic clarity.\n"
        "              * These names are not case sensitive: 'nonic' == 'Nonic', etc.\n"
        "              * Higher and higher order polynomials will take longer and longer\n"
        "                to run!\n"
        "              * If you wish to apply a nonlinear warp, you have to supply\n"
        "                a parameter file with -1Dparam_apply and also specify the\n"
        "                warp type with -nwarp.  The number of parameters in the\n"
        "                file (per line) must match the warp type:\n"
        "                   bilinear =  43   [for all nonlinear warps, the final]\n"
        "                   cubic    =  64   [4 'parameters' are fixed values to]\n"
        "                   quintic  = 172   [normalize the coordinates to -1..1]\n"
        "                   heptic   = 364   [for the nonlinear warp functions. ]\n"
        "                   nonic    = 664\n"
        "                In all these cases, the first 12 parameters are the\n"
        "                affine parameters (shifts, rotations, etc.), and the\n"
        "                remaining parameters define the nonlinear part of the warp\n"
        "                (polynomial coefficients); thus, the number of nonlinear\n"
        "                parameters over which the optimization takes place is\n"
        "                the number in the table above minus 16.\n"
        "               * The actual polynomial functions used are products of\n"
        "                 Legendre polynomials, but the symbolic names used in\n"
        "                 the header line in the '-1Dparam_save' output just\n"
        "                 express the polynomial degree involved; for example,\n"
        "                      quint:x^2*z^3:z\n"
        "                 is the name given to the polynomial warp basis function\n"
        "                 whose highest power of x is 2, is independent of y, and\n"
        "                 whose highest power of z is 3; the 'quint' indicates that\n"
        "                 this was used in '-nwarp quintic'; the final ':z' signifies\n"
        "                 that this function was for deformations in the (DICOM)\n"
        "                 z-direction (+z == Superior).\n"
        "        ==>>*** You can further control the form of the polynomial warps\n"
        "                (but not the bilinear warp!) by restricting their degrees\n"
        "                of freedom in 2 different ways.\n"
        "                ++ You can remove the freedom to have the nonlinear\n"
        "                   deformation move along the DICOM x, y, and/or z axes.\n"
        "                ++ You can remove the dependence of the nonlinear\n"
        "                   deformation on the DICOM x, y, and/or z coordinates.\n"
        "                ++ To illustrate with the six second order polynomials:\n"
        "                      p2_xx(x,y,z) = x*x  p2_xy(x,y,z) = x*y\n"
        "                      p2_xz(x,y,z) = x*z  p2_yy(x,y,z) = y*y\n"
        "                      p2_yz(x,y,z) = y*z  p2_zz(x,y,z) = z*z\n"
        "                   Unrestricted, there are 18 parameters associated with\n"
        "                   these polynomials, one for each direction of motion (x,y,z)\n"
        "                   * If you remove the freedom of the nonlinear warp to move\n"
        "                     data in the z-direction (say), then there would be 12\n"
        "                     parameters left.\n"
        "                   * If you instead remove the freedom of the nonlinear warp\n"
        "                     to depend on the z-coordinate, you would be left with\n"
        "                     3 basis functions (p2_xz, p2_yz, and p2_zz would be\n"
        "                     eliminated), each of which would have x-motion, y-motion,\n"
        "                     and z-motion parameters, so there would be 9 parameters.\n"
        "                ++ To fix motion along the x-direction, use the option\n"
        "                   '-nwarp_fixmotX' (and '-nwarp_fixmotY' and '-nwarp_fixmotZ).\n"
        "                ++ To fix dependence of the polynomial warp on the x-coordinate,\n"
        "                   use the option '-nwarp_fixdepX' (et cetera).\n"
        "                ++ These coordinate labels in the options (X Y Z) refer to the\n"
        "                   DICOM directions (X=R-L, Y=A-P, Z=I-S).  If you would rather\n"
        "                   fix things along the dataset storage axes, you can use\n"
        "                   the symbols I J K to indicate the fastest to slowest varying\n"
        "                   array dimensions (e.g., '-nwarp_fixdepK').\n"
        "                   * Mixing up the X Y Z and I J K forms of parameter freezing\n"
        "                     (e.g., '-nwarp_fixmotX -nwarp_fixmotJ') may cause trouble!\n"
        "                ++ If you input a 2D dataset (a single slice) to be registered\n"
        "                   with '-nwarp', the program automatically assumes '-nwarp_fixmotK'\n"
        "                   and '-nwarp_fixdepK' so there are no out-of-plane parameters\n"
        "                   or dependence.  The number of nonlinear parameters is then:\n"
        "                     2D: cubic = 14 ; quintic =  36 ; heptic =  66 ; nonic = 104.\n"
        "                     3D: cubic = 48 ; quintic = 156 ; heptic = 348 ; nonic = 648.\n"
        "                     [ n-th order: 2D = (n+4)*(n-1) ; 3D = (n*n+7*n+18)*(n-1)/2 ]\n"
        "                ++ Note that these '-nwarp_fix' options have no effect on the\n"
        "                   affine part of the warp -- if you want to constrain that as\n"
        "                   well, you'll have to use the '-parfix' option.\n"
        "                   * However, for 2D images, the affine part will automatically\n"
        "                     be restricted to in-plane (6 parameter) 'motions'.\n"
        "                ++ If you save the warp parameters (with '-1Dparam_save') when\n"
        "                   doing 2D registration, all the parameters will be saved, even\n"
        "                   the large number of them that are fixed to zero. You can use\n"
        "                   '1dcat -nonfixed' to remove these columns from the 1D file if\n"
        "                   you want to further process the varying parameters (e.g., 1dsvd).\n"
        "              **++ The mapping from I J K to X Y Z (DICOM coordinates), where the\n"
        "                   '-nwarp_fix' constraints are actually applied, is very simple:\n"
        "                   given the command to fix K (say), the coordinate X, or Y, or Z\n"
        "                   whose direction most closely aligns with the dataset K grid\n"
        "                   direction is chosen.  Thus, for coronal images, K is in the A-P\n"
        "                   direction, so '-nwarp_fixmotK' is translated to '-nwarp_fixmotY'.\n"
        "                   * This simplicity means that using the '-nwarp_fix' commands on\n"
        "                     oblique datasets is problematic.  Perhaps it would work in\n"
        "                     combination with the '-EPI' option, but that has not been tested.\n"
        "\n"
        "-nwarp NOTES:\n"
        "-------------\n"
        "* -nwarp is slow - reeeaaallll slow - use it with OpenMP!\n"
        "* Check the results to make sure the optimizer didn't run amok!\n"
        "   (You should ALWAYS do this with any registration software.)\n"
        "* For the nonlinear warps, the largest coefficient allowed is\n"
        "   set to 0.10 by default.  If you wish to change this, use an\n"
        "   option like '-nwarp_parmax 0.05' (to make the allowable amount\n"
        "   of nonlinear deformation half the default).\n"
        "  ++ N.B.: Increasing the maximum past 0.10 may give very bad results!!\n"
        "* If you use -1Dparam_save, then you can apply the nonlinear\n"
        "   warp to another dataset using -1Dparam_apply in a later\n"
        "   3dAllineate run. To do so, use '-nwarp xxx' in both runs\n"
        "   , so that the program knows what the extra parameters in\n"
        "   the file are to be used for.\n"
        "  ++ Bilinear: 43 values are saved in 1 row of the param file.\n"
        "  ++ The first 12 are the affine parameters\n"
        "  ++ The next 27 are the D1,D2,D3 matrix parameters (cf. infra).\n"
        "  ++ The final 'extra' 4 values are used to specify\n"
        "      the center of coordinates (vector Xc below), and a\n"
        "      pre-computed scaling factor applied to parameters #13..39.\n"
        "  ++ For polynomial warps, a similar format is used (mutatis mutandis).\n"
        "* The option '-nwarp_save sss' lets you save a 3D dataset of the\n"
        "  the displacement field used to create the output dataset.  This\n"
        "  dataset can be used in program 3dNwarpApply to warp other datasets.\n"
        "  ++ If the warp is symbolized by x -> w(x) [here, x is a DICOM 3-vector],\n"
        "     then the '-nwarp_save' dataset contains w(x)-x; that is, it contains\n"
        "     the warp displacement of each grid point from its grid location.\n"
        "  ++ Also see program 3dNwarpCalc for other things you can do with this file:\n"
        "       warp inversion, catenation, square root, ...\n"
        "\n"
        "* Bilinear warp formula:\n"
        "   Xout = inv[ I + {D1 (Xin-Xc) | D2 (Xin-Xc) | D3 (Xin-Xc)} ] [ A Xin ]\n"
        "  where Xin  = input vector  (base dataset coordinates)\n"
        "        Xout = output vector (source dataset coordinates)\n"
        "        Xc   = center of coordinates used for nonlinearity\n"
        "               (will be the center of the base dataset volume)\n"
        "        A    = matrix representing affine transformation (12 params)\n"
        "        I    = 3x3 identity matrix\n"
        "    D1,D2,D3 = three 3x3 matrices (the 27 'new' parameters)\n"
        "               * when all 27 parameters == 0, warp is purely affine\n"
        "     {P|Q|R} = 3x3 matrix formed by adjoining the 3-vectors P,Q,R\n"
        "    inv[...] = inverse 3x3 matrix of stuff inside '[...]'\n"
        "* The inverse of a bilinear transformation is another bilinear\n"
        "   transformation.  Someday, I may write a program that will let\n"
        "   you compute that inverse transformation, so you can use it for\n"
        "   some cunning and devious purpose.\n"
        "* If you expand the inv[...] part of the above formula in a 1st\n"
        "   order Taylor series, you'll see that a bilinear warp is basically\n"
        "   a quadratic warp, with the additional feature that its inverse\n"
        "   is directly computable (unlike a pure quadratic warp).\n"
        "* 'bilinearD' means the matrices D1, D2, and D3 with be constrained\n"
        "  to be diagonal (a total of 9 nonzero values), rather than full\n"
        "  (a total of 27 nonzero values).  This option is much faster.\n"
        "* Is '-nwarp bilinear' useful?  Try it and tell me!\n"
        "* Unlike a bilinear warp, the polynomial warps cannot be exactly\n"
        "  inverted.  At some point, I'll write a program to compute an\n"
        "  approximate inverse, if there is enough clamor for such a toy.\n"
        "\n"
        "===========================================================================\n"
       ) ;
#endif

     } else {  /* now unreachable */

       printf(
        "\n"
        " [[[ To see a plethora of advanced/experimental options, use '-HELP'. ]]]\n");

     }

     PRINT_AFNI_OMP_USAGE("3dAllineate",
       "* OpenMP may or may not speed up the program significantly.  Limited\n"
       "   tests show that it provides some benefit, particularly when using\n"
       "   the more complicated interpolation methods (e.g., '-cubic' and/or\n"
       "   '-final wsinc5'), for up to 3-4 CPU threads.\n"
       "* But the speedup is definitely not linear in the number of threads, alas.\n"
       "   Probably because my parallelization efforts were pretty limited.\n"
      ) ;

     PRINT_COMPILE_DATE ; exit(0);
}

/*---------------------------------------------------------------------------*/
/*============================ Ye Olde Main Programme =======================*/
/* Note the GA_setup struct 'stup' below.
   This struct contains all the alignment setup information,
   and is passed to functions in mri_genalign.c to do lots of things,
   like evaluate the cost functional, optimize the cost, and so forth. */
/*---------------------------------------------------------------------------*/

int main( int argc , char *argv[] )
{
   GA_setup stup ;                   /* holds all the setup info */
   THD_3dim_dataset *dset_out=NULL ;
   MRI_IMAGE *im_base, *im_targ, *im_weig=NULL, *im_mask=NULL, *qim ;
   MRI_IMAGE *im_bset, *im_wset, *im_tmask=NULL ;
   int iarg , ii,jj,kk , nmask=0 , nfunc , rr , ntask , ntmask=0 , nnz ;
   int   nx_base,ny_base,nz_base , nx_targ,ny_targ,nz_targ , nxy_base ;
   float dx_base,dy_base,dz_base , dx_targ,dy_targ,dz_targ , dxyz_top ;
   int   nxyz_base[3] , nxyz_targ[3] , nxyz_dout[3] ;
   float dxyz_base[3] , dxyz_targ[3] , dxyz_dout[3] ;
   int nvox_base ;
   float v1,v2 , xxx_p,yyy_p,zzz_p,siz , xxx_m,yyy_m,zzz_m , xxx,yyy,zzz , xc,yc,zc ;
   float xxc,yyc,zzc ; int CMbad=0 ; /* 26 Feb 2020 */
   int pad_xm=0,pad_xp=0 , pad_ym=0,pad_yp=0 , pad_zm=0,pad_zp=0 ;
   int tfdone=0;  /* stuff for -twofirst */
   float tfparm[PARAM_MAXTRIAL+2][MAXPAR];  /* +2 for some extra cases */
   float ffparm[PARAM_MAXTRIAL+2][MAXPAR];  /* not really used yet */
   float tfcost[PARAM_MAXTRIAL+2] ;
   int   tfindx[PARAM_MAXTRIAL+2] ;
   int skip_first=0 , didtwo , targ_kind, skipped=0 , nptwo=6 ;
   int targ_was_vector=0, targ_vector_kind=-1 ; MRI_IMAGE *im_targ_vector=NULL ;
   double ctim=0.0,dtim , rad , conv_rad ;
   float **parsave=NULL ;
   mat44 *matsave=NULL ;
   mat44 targ_cmat,base_cmat,base_cmat_inv,targ_cmat_inv,mast_cmat,mast_cmat_inv,
         qmat,wmat ;
   MRI_IMAGE *apply_im = NULL ;
   float *apply_far    = NULL ;
   int apply_nx=0, apply_ny=0, apply_mode=0, nparam_free , diffblur=1 ;
   float cost, cost_ini ;
   mat44 cmat_bout , cmat_tout , aff12_xyz ;
   int   nxout=0,nyout=0,nzout=0 ;
   float dxout,dyout,dzout ;
   floatvec *allcost ;   /* 19 Sep 2007 */
   float     allpar[MAXPAR] ;
   float xsize , ysize , zsize ;  /* 06 May 2008: box size */
   float bfac ;                   /* 14 Oct 2008: brick factor */
   int twodim_code=0 , xx_code=0 , yy_code=0 , zz_code=0 ;

   /*----- input parameters, to be filled in from the options -----*/

   THD_3dim_dataset *dset_base = NULL ;
   THD_3dim_dataset *dset_targ = NULL ;
   THD_3dim_dataset *dset_mast = NULL ;
   THD_3dim_dataset *dset_weig = NULL ;
   int tb_mast                 = 0 ;            /* for -master SOURCE/BASE */
   int auto_weight             = 3 ;            /* -autobbox == default */
   float auto_wclip            = 0.0f ;         /* 31 Jul 2007 */
   float auto_wpow             = 1.0f ;         /* 10 Sep 2007 */
   char *auto_string           = "-autobox" ;
   int auto_dilation           = 0 ;            /* for -automask+N */
   int wtspecified             = 0 ;            /* 10 Sep 2007 (was weight specified?) */
   double dxyz_mast            = 0.0  ;         /* implemented 24 Jul 2007 */
   int meth_code               = GA_MATCH_HELLINGER_SCALAR ;
   int sm_code                 = GA_SMOOTH_GAUSSIAN ;
   float sm_rad                = 0.0f ;
   float fine_rad              = 0.0f ;
   int floatize                = 0 ;            /* off by default */
   int twopass                 = 1 ;            /* on by default */
   int twofirst                = 1 ;            /* on by default */
   int zeropad                 = 1 ;            /* on by default */
   char *prefix                = NULL ;         /* off by default */
   char *wtprefix              = NULL ;         /* off by default */
   char *param_save_1D         = NULL ;         /* off by default */
   char *matrix_save_1D        = NULL ;         /* 23 Jul 2007 */
   char *apply_1D              = NULL ;         /* off by default */
   int interp_code             = MRI_LINEAR ;
   int got_interp              = 0 ;
   int npt_match               = -47 ;          /* 47%, that is */
   int final_interp            = MRI_CUBIC ;
   int got_final               = 0 ;
   int warp_code               = WARP_AFFINE ;
   char warp_code_string[64]   = "\0" ;         /* 22 Feb 2010 */
   int warp_freeze             = 0 ;            /* off by default */
   int do_small                = 0 ;            /* 12 May 2020 */
   int nparopt                 = 0 ;
   MRI_IMAGE *matini           = NULL ;
   int tbest                   = DEFAULT_TBEST; /* default=try best 5 */
   int num_rtb                 = 99 ;           /* 28 Aug 2008 */
   int nocast                  = 0 ;            /* 29 Aug 2008 */
   param_opt paropt[MAXPAR] ;
   float powell_mm             = 0.0f ;
   float powell_aa             = 0.0f ;
   float conv_mm               = 0.001f ;       /* millimeters */
   float nmask_frac            = -1.0;          /* use default for voxel fraction */
   int matorder                = MATORDER_SDU ; /* matrix mult order */
   int smat                    = SMAT_LOWER ;   /* shear matrix triangle */
   int dcode                   = DELTA_AFTER ;  /* shift after */
   int meth_check_count        = 0 ;            /* don't do it */
   int meth_check[NMETH+1] ;
   int meth_median_replace     = 0 ;            /* don't do it */
   char *save_hist             = NULL ;         /* don't save it */
   long seed                   = 7654321 ;      /* random? */
   int XYZ_warp                = 0 ;            /* off by default */
   double hist_pow             = 0.0 ;
   int hist_nbin               = 0 ;
   int epi_fe                  = -1 ;           /* off by default */
   int epi_pe                  = -1 ;
   int epi_se                  = -1 ;
   int epi_targ                = -1 ;
   int replace_base            = 0 ;            /* off by default */
   int replace_meth            = 0 ;            /* off by default */
   int usetemp                 = 0 ;            /* off by default */
   int nmatch_setup            = 98765 ;
   int ignout                  = 0 ;            /* 28 Feb 2007 */
   int    hist_mode            = 0 ;            /* 08 May 2007 */
   float  hist_param           = 0.0f ;
   int    hist_setbyuser       = 0 ;
   int    do_cmass             = 0 ;            /* 30 Jul 2007 */
   int    do_refinal           = 1 ;            /* 14 Nov 2007 */
   int    use_realaxes         = 0 ;            /* 10 Oct 2014 */

   int auto_tdilation          = 0 ;            /* for -source_automask+N */
   int auto_tmask              = 0 ;
   char *auto_tstring          = NULL ;
   int fill_source_mask        = 0 ;

   int bloktype                = GA_BLOK_RHDD ; /* 20 Aug 2007 */
   float blokrad               = 6.54321f ;
   int blokmin                 = 0 ;

   int do_allcost              = 0 ;            /* 19 Sep 2007 */
   int do_save_pearson_map     = 0 ;            /* 25 Jan 2021 */
   char *save_pearson_prefix   = NULL ;

   MRI_IMAGE *allcostX1D       = NULL ;         /* 02 Sep 2008 */
   char *allcostX1D_outname    = NULL ;

   int   nwarp_pass            = 0 ;
   int   nwarp_type            = WARP_CUBIC ;
   float nwarp_parmax          = 0.10f ;         /* 05 Jan 2011 */
   int   nwarp_flags           = 0 ;             /* 29 Oct 2010 */
   int   nwarp_itemax          = 0 ;
   int   nwarp_fixaff          = 0 ;             /* 26 Nov 2010 */
   int   nwarp_fixmotX         = 0 ;             /* 07 Dec 2010 */
   int   nwarp_fixdepX         = 0 ;
   int   nwarp_fixmotY         = 0 ;
   int   nwarp_fixdepY         = 0 ;
   int   nwarp_fixmotZ         = 0 ;
   int   nwarp_fixdepZ         = 0 ;
   int   nwarp_fixmotI         = 0 ;
   int   nwarp_fixdepI         = 0 ;
   int   nwarp_fixmotJ         = 0 ;
   int   nwarp_fixdepJ         = 0 ;
   int   nwarp_fixmotK         = 0 ;
   int   nwarp_fixdepK         = 0 ;
   char *nwarp_save_prefix     = NULL ;          /* 10 Dec 2010 */
   int   nwarp_meth_code       = 0 ;             /* 15 Dec 2010 */

   int    micho_zfinal         = 0 ;                  /* 24 Feb 2010 */
   double micho_mi             = DEFAULT_MICHO_MI  ;  /* -lpc+ stuff */
   double micho_nmi            = DEFAULT_MICHO_NMI ;
   double micho_crA            = DEFAULT_MICHO_CRA ;
   double micho_hel            = DEFAULT_MICHO_HEL ;
   double micho_ov             = DEFAULT_MICHO_OV  ;  /* 02 Mar 2010 */
   int    micho_fallthru       = 0 ;                  /* 19 Nov 2016 */

   int do_zclip                = 0 ;             /* 29 Oct 2010 */

   bytevec *emask              = NULL ;          /* 14 Feb 2013 */

   int do_xflip_bset           = 0 ;             /* 18 Jun 2019 */

#undef ALLOW_UNIFIZE   /* I decided this was a bad idea */
#ifdef ALLOW_UNIFIZE
   int do_unifize_base         = 0 ;             /* 23 Dec 2016 */
   int do_unifize_targ         = 0 ;             /* not implemented */
   MRI_IMAGE *im_ubase         = NULL ;
   MRI_IMAGE *im_utarg         = NULL ;
#endif

#define APPLYING ( apply_1D != NULL || apply_mode != 0 )  /* 13 Mar 2017 */

   /**----------------------------------------------------------------------*/
   /**----------------- Help the pitifully ignorant user? -----------------**/

   AFNI_SETUP_OMP(0) ;  /* 24 Jun 2013 */

   /*--- A couple thousand lines of help code goes here for some reason ---*/

   if( argc < 2 || strcmp(argv[1],"-help")==0 ||
                   strcmp(argv[1],"-HELP")==0 || strcmp(argv[1],"-POMOC")==0 ){

     Allin_Help() ; exit(0) ;
   }

   /**-------------------------------------------------------------------**/
   /**-------------------- bookkeeping and marketing --------------------**/
   /**-------------------------------------------------------------------**/

#if defined(USING_MCW_MALLOC) && !defined(USE_OMP)
   enable_mcw_malloc() ;
#else
# if 0
   if( AFNI_yesenv("ALLIN_DEBUG") ) enable_mcw_malloc() ;
# endif
#endif

   mainENTRY("3dAllineate"); machdep();
   AFNI_logger("3dAllineate",argc,argv);
   PRINT_VERSION("3dAllineate"); AUTHOR("Zhark the Registrator");
   THD_check_AFNI_version("3dAllineate"); /* no longer does anything */
   (void)COX_clock_time() ;

   /*-- initialize -final interp from environment? [25 Nov 2018] --*/

   ii = INTERP_code(my_getenv("AFNI_3dAllineate_final")) ;
   if( ii >= 0 ){ final_interp = ii ; got_final = 1 ; }

   /**--- process command line options ---**/

   iarg = 1 ;
   while( iarg < argc && argv[iarg][0] == '-' ){

     /*------*/

#ifdef ALLOW_UNIFIZE
     if( strcmp(argv[iarg],"-unifize_base") == 0 ){    /* 23 Dec 2016 */
       do_unifize_base++ ; iarg++ ; continue ;
     }
# if 0
     if( strcmp(argv[iarg],"-unifize_source") == 0 ){  /* 23 Dec 2016 */
       do_unifize_targ++ ; iarg++ ; continue ;
     }
# endif
#endif

     /*------*/

     if( strcmp(argv[iarg],"-smallrange") == 0 ){ /* 12 May 2020 */
       do_small++ ; iarg++ ; continue ;
     }

     /*------*/

     if( strcmp(argv[iarg],"-realaxes") == 0 ){  /* 10 Oct 2014 */
       use_realaxes++ ; iarg++ ; continue ;
     }

     /*------*/

     if( strcmp(argv[iarg],"-xflipbase") == 0 ){  /* 18 Jun 2019 [SECRET] */
       do_xflip_bset = 1 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-zclip") == 0 || strcmp(argv[iarg],"-noneg") == 0 ){     /* 29 Oct 2010 */
       do_zclip++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-nwarp_save",11) == 0 ){  /* 10 Dec 2010 = SECRET */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: '%s' '%s' :-(",argv[iarg-1],argv[iarg]) ;
       if( strcmp(argv[iarg],"NULL") == 0 ) nwarp_save_prefix = NULL ;
       else                                 nwarp_save_prefix = argv[iarg] ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-nwarp_parmax") == 0 ){    /* 05 Jan 2011 = SECRET */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       nwarp_parmax = (float)strtod(argv[iarg],NULL) ;
       if( nwarp_parmax <= 0.0f || nwarp_parmax > 1.0f )
         ERROR_exit("Illegal value (%g) after '%s' :-(",nwarp_parmax,argv[iarg-1]) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-nwarp_fix",10) == 0 ){  /* 07 Dec 2010 = SECRET */
       char *aaa = argv[iarg]+10 , dcod ;
       if( strlen(aaa) < 4 ) ERROR_exit("don't understand option %s",argv[iarg]) ;
       dcod = toupper(aaa[3]) ;
       if( strncmp(aaa,"mot",3) == 0 ){            /* -nwarp_fixmot */
         switch( dcod ){
           case 'X': nwarp_fixmotX = 1 ; break ;
           case 'Y': nwarp_fixmotY = 1 ; break ;
           case 'Z': nwarp_fixmotZ = 1 ; break ;
           case 'I': nwarp_fixmotI = 1 ; break ;
           case 'J': nwarp_fixmotJ = 1 ; break ;
           case 'K': nwarp_fixmotK = 1 ; break ;
           default:  ERROR_exit("can't decode option %s",argv[iarg]) ;
         }
       } else if( strncmp(aaa,"dep",3) == 0 ){     /* -nwarp_fixdep */
         switch( dcod ){
           case 'X': nwarp_fixdepX = 1 ; break ;
           case 'Y': nwarp_fixdepY = 1 ; break ;
           case 'Z': nwarp_fixdepZ = 1 ; break ;
           case 'I': nwarp_fixdepI = 1 ; break ;
           case 'J': nwarp_fixdepJ = 1 ; break ;
           case 'K': nwarp_fixdepK = 1 ; break ;
           default:  ERROR_exit("can't decode option %s",argv[iarg]) ;
         }
       } else {
         ERROR_exit("don't know option %s",argv[iarg]) ;
       }
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcasecmp(argv[iarg],"-nwarp_HERMITE") == 0 ){  /** 28 Mar 2013: SUPER-SECRET **/
       GA_setup_polywarp(GA_HERMITE) ; nwarp_parmax = 0.0444f ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-nwarp") == 0 ){     /* 03 Apr 2008 = SECRET */
       nwarp_pass = 1 ; iarg++ ;

       WARNING_message(" !! Use program 3dQwarp instead of 3dAllineate -nwarp !!" ) ;
       WARNING_message(" !! 3dAllineate -nwarp is obsolete and inferior :(    !!" ) ;
       WARNING_message(" (( YOU HAVE BEEN WARNED -- here lurketh the dragons  ))" ) ;

       if( iarg >= argc ){
         ERROR_exit("need a warp type after '-nwarp' :-(") ;
       } else if( strncasecmp(argv[iarg],"bil",3) == 0 ){
         nwarp_type = WARP_BILINEAR ;
         if( strstr(argv[iarg],"D") != NULL ) nwarp_flags = 1 ; /* 29 Oct 2010 */
       } else if( strncasecmp(argv[iarg],"cub",3)   == 0 ||
                  strncasecmp(argv[iarg],"poly3",5) == 0   ){   /* 13 Nov 2010 */
         nwarp_type = WARP_CUBIC ;
       } else if( strncasecmp(argv[iarg],"qui",3)   == 0 ||
                  strncasecmp(argv[iarg],"poly5",5) == 0   ){   /* 15 Nov 2010 */
         nwarp_type = WARP_QUINT ;
       } else if( strncasecmp(argv[iarg],"hep",3)   == 0 ||
                  strncasecmp(argv[iarg],"poly7",5) == 0   ){   /* 15 Nov 2010 */
         nwarp_type = WARP_HEPT ;
       } else if( strncasecmp(argv[iarg],"non",3)   == 0 ||
                  strncasecmp(argv[iarg],"poly9",5) == 0   ){   /* 17 Nov 2010 */
         nwarp_type = WARP_NONI ;
       } else {
         ERROR_exit("unknown -nwarp type '%s' :-(",argv[iarg]) ;
       }
       nwarp_fixaff = ( strstr(argv[iarg],"FA") != NULL ) ;
       warp_code = WARP_AFFINE ; iarg++ ;

       if( iarg < argc && isdigit(argv[iarg][0]) ){      /** really secret **/
         nwarp_itemax = (int)strtod(argv[iarg],NULL) ;
         iarg++ ;
       }

       if( iarg < argc && isalpha(argv[iarg][0]) ){
         nwarp_meth_code = meth_name_to_code(argv[iarg]) ;
         if( nwarp_meth_code > 0 ) iarg++ ;
       }

       /* change some other parameters from their defaults */

       do_refinal = 0 ;
       continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-norefinal") == 0 ){ /* 14 Nov 2007 */
       do_refinal = 0 ; iarg++ ; continue ;      /* SECRET OPTION */
     }

     /*-----*/

     if( strcmp(argv[iarg],"-allcost") == 0 ){   /* 19 Sep 2007 */
       do_allcost = 1 ; iarg++ ; continue ;      /* SECRET OPTIONS */
     }
     if( strcmp(argv[iarg],"-allcostX") == 0 ){
       do_allcost = -1 ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-allcostX1D") == 0 ){ /* 02 Sep 2008 */
       MRI_IMAGE *qim ;
       do_allcost = -2 ;
       if(strcmp(argv[++iarg],"IDENTITY") == 0)
            allcostX1D = mri_identity_params();
       else {
          qim = mri_read_1D( argv[iarg] ) ;
          if( qim == NULL )
            ERROR_exit("Can't read -allcostX1D '%s' :-(",argv[iarg]) ;
          allcostX1D = mri_transpose(qim) ; mri_free(qim) ;
       }
       if( allcostX1D->nx < 12 )
         ERROR_exit("-allcostX1D '%s' has only %d values per row :-(" ,
                    argv[iarg] , allcostX1D->nx ) ;
       allcostX1D_outname = strdup(argv[++iarg]) ;
       ++iarg ; continue ;
     }

     /*-----*/

     if( strcasecmp(argv[iarg],"-PearSave") == 0 ||
         strcasecmp(argv[iarg],"-SavePear") == 0   ){  /* 25 Jan 2021 */
       static char *pppname = "PearSave.nii.gz" ;
       do_save_pearson_map = 1 ; iarg++ ;
       if( !THD_filename_ok(argv[iarg]) ){
         WARNING_message("Option '%s' has illegal prefix '%s' - replacing with '%s'",
                         argv[iarg-1] , argv[iarg] , pppname ) ;
         save_pearson_prefix = strdup(pppname) ;
       } else {
         save_pearson_prefix = strdup(argv[iarg]) ;
       }
       ++iarg ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-cmass",6) == 0 ){
       if( argv[iarg][6] == '+' ){
         if (strchr(argv[iarg]+6,'a')) {
            do_cmass = -1; /* ZSS */
         } else {
            do_cmass =    (strchr(argv[iarg]+6,'x') != NULL)
                      + 2*(strchr(argv[iarg]+6,'y') != NULL)
                      + 4*(strchr(argv[iarg]+6,'z') != NULL) ;
         }
         if( do_cmass == 0 )
           ERROR_exit("Don't understand coordinates in '%s :-(",argv[iarg]) ;
       } else {
         do_cmass = 7 ;  /* all coords */
       }
       iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-nocmass") == 0 ){
       do_cmass = 0 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-ignout") == 0 ){               /* SECRET OPTION */
       GA_set_outval(1.e+33); ignout = 1; iarg++; continue; /* 28 Feb 2007  */
     }

     /*-----*/

     if( strcmp(argv[iarg],"-matini") == 0 ){
       if( matini != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( strncmp(argv[iarg],"1D:",3) == 0 ||
           (strchr(argv[iarg],' ') == NULL && argv[iarg][0] != '&') ){
         matini = mri_read_1D( argv[iarg] ) ;
         if( matini == NULL ) ERROR_exit("Can't read -matini file '%s' :-(",argv[iarg]);
       } else {
         matini = mri_matrix_evalrpn( argv[iarg] ) ;
         if( matini == NULL ) ERROR_exit("Can't evaluate -matini expression :-(");
       }
       if( matini->nx < 3 || matini->ny < 3 )
         ERROR_exit("-matini matrix has nx=%d and ny=%d (should be at least 3) :-(",
                    matini->nx,matini->ny) ;
       else if( matini->nx > 3 || matini->ny > 4 )
         WARNING_message("-matini matrix has nx=%d and ny=%d (should be 3x4) :-(",
                    matini->nx,matini->ny) ;

       WARNING_message("-matini is not yet implemented! :-(") ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-mast_dxyz") == 0 ||
         strcmp(argv[iarg],"-dxyz_mast") == 0 ||
         strcmp(argv[iarg],"-newgrid"  ) == 0   ){

       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       dxyz_mast = strtod(argv[iarg],NULL) ;
       if( dxyz_mast <= 0.0 )
         ERROR_exit("Illegal value '%s' after -mast_dxyz :-(",argv[iarg]) ;
       if( dxyz_mast <= 0.5 )
         WARNING_message("Small value %g after -mast_dxyz :-(",dxyz_mast) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-nmsetup") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       nmatch_setup = (int)strtod(argv[iarg],NULL) ;
       if( nmatch_setup < 9999 ) nmatch_setup = 23456 ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-master",6) == 0 ){
       if( dset_mast != NULL || tb_mast )
         ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( strcmp(argv[iarg],"SOURCE") == 0 ){  /* 19 Jul 2007 */
         tb_mast = 1 ;
       } else if( strcmp(argv[iarg],"BASE") == 0 ){
         tb_mast = 2 ;
       } else {
         dset_mast = THD_open_dataset( argv[iarg] ) ;
         if( dset_mast == NULL )
           ERROR_exit("can't open -master dataset '%s' :-(",argv[iarg]);
       }
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-seed") == 0 ){   /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       seed = (long)strtod(argv[iarg],NULL) ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-powell") == 0 ){  /* SECRET OPTION */
       if( ++iarg >= argc-1 ) ERROR_exit("no arguments after '%s' :-(",argv[iarg-1]) ;
       powell_mm = (float)strtod(argv[iarg++],NULL) ;
       powell_aa = (float)strtod(argv[iarg++],NULL) ;
       if( powell_mm < 1.0f ) powell_mm = 1.0f ;
       if( powell_aa < 1.0f ) powell_aa = 1.0f ;
       continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-weight_frac",11) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       nmask_frac = atof( argv[iarg] ) ;
       if( nmask_frac < 0.0f || nmask_frac > 1.0f )
         ERROR_exit("-weight_frac must be between 0.0 and 1.0 (have '%s') :-(",argv[iarg]);
       wtspecified = 1 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-weight",6) == 0 ){
       auto_weight = 0 ;
       if( dset_weig != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       dset_weig = THD_open_dataset( argv[iarg] ) ;
       if( dset_weig == NULL ) ERROR_exit("can't open -weight dataset '%s' :-(",argv[iarg]);
       wtspecified = 1 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-autoweight",11) == 0 ){
       char *cpt ;
       int   cpt_offset = 2; /* offset for exponent str [rickr 23 Apr 2019] */
                             /* allow ** or ^ (to avoid shell protection)   */
       if( dset_weig != NULL ) ERROR_exit("Can't use -autoweight AND -weight :-(") ;
       auto_weight = 1 ; auto_string = argv[iarg] ;
       cpt = strstr(auto_string,"+") ;
       if( cpt != NULL && *(cpt+1) != '\0' )      /* 31 Jul 2007 */
         auto_wclip = (float)strtod(cpt+1,NULL) ;
       cpt = strstr(auto_string,"**") ;
       if( cpt == NULL ) {
          /* allow ** or ^ (to avoid shell protection) [rickr 23 Apr 2019] */
          cpt = strstr(auto_string,"^") ;
          cpt_offset = 1;
       }
       if( cpt != NULL && *(cpt+cpt_offset) != '\0' )      /* 10 Sep 2007 */
         auto_wpow = (float)strtod(cpt+cpt_offset,NULL) ;
       wtspecified = 1 ; iarg++ ; continue ;
     }

     if( strncmp(argv[iarg],"-automask",9) == 0 ){
       if( dset_weig != NULL ) ERROR_exit("Can't use -automask AND -weight :-(") ;
       auto_weight = 2 ; auto_string = argv[iarg] ;
       if( auto_string[9] == '+' && auto_string[10] != '\0' )
         auto_dilation = (int)strtod(auto_string+10,NULL) ;
       wtspecified = 1 ; iarg++ ; continue ;
     }

     if( strncmp(argv[iarg],"-noauto",6) == 0 ||
         strncmp(argv[iarg],"-nomask",6) == 0   ){
       wtspecified = 1 ; auto_weight = 0 ; iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-autobox") == 0 ){
       wtspecified = 1 ; auto_weight = 3 ; auto_string = "-autobox" ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-source_mask") == 0 ){  /* 07 Aug 2007 */
       byte *mmm ; THD_3dim_dataset *dset_tmask ;
       if( im_tmask != NULL )
         ERROR_exit("Can't use -source_mask twice :-(") ;
       if( auto_tmask )
         ERROR_exit("Can't use -source_mask AND -source_automask :-(") ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       dset_tmask = THD_open_dataset( argv[iarg] ) ;
       if( dset_tmask == NULL )
         ERROR_exit("can't open -source_mask dataset '%s' :-(",argv[iarg]);
       mmm = THD_makemask( dset_tmask , 0 , 1.0f,-1.0f ) ;
       if( mmm == NULL )
         ERROR_exit("Can't use -source_mask '%s' for some reason :-(",argv[iarg]) ;
       im_tmask = mri_new_vol_empty(
                   DSET_NX(dset_tmask),DSET_NY(dset_tmask),DSET_NZ(dset_tmask) ,
                   MRI_byte ) ;
       DSET_delete(dset_tmask) ; /* ZSS: Moved here cause that's
                                    right and proper*/
       mri_fix_data_pointer( mmm , im_tmask ) ;
       ntmask = THD_countmask( im_tmask->nvox , mmm ) ;
       if( ntmask < 666 )
         ERROR_exit("Too few (%d) voxels in -source_mask :-(",ntmask) ;
       if( verb > 1 ) INFO_message("%d voxels in -source_mask",ntmask) ;
       iarg++ ; fill_source_mask = 1 ; continue ;
     }

     if( strncmp(argv[iarg],"-source_automask",16) == 0 ){  /* 07 Aug 2007 */
       if( im_tmask != NULL )
         ERROR_exit("Can't use -source_automask AND -source_mask :-(") ;
       auto_tmask = 1 ; auto_tstring = argv[iarg] ;
       if( auto_tstring[16] == '+' && auto_string[17] != '\0' )
         auto_tdilation = (int)strtod(auto_tstring+17,NULL) ;
       iarg++ ; fill_source_mask = 1 ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-wtprefix",6) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: '%s' '%s' :-(",argv[iarg-1],argv[iarg]) ;
       wtprefix = argv[iarg] ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-savehist") == 0 ){  /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: '%s' '%s' :-(",argv[iarg-1],argv[iarg]) ;
       save_hist = argv[iarg] ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-histpow") == 0 ){   /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       hist_pow = strtod(argv[iarg],NULL) ;
       set_2Dhist_hpower(hist_pow) ;
       iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-histbin") == 0 ){   /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       hist_nbin = (int)strtod(argv[iarg],NULL) ;
       hist_mode = 0 ; hist_param = 0.0f ; hist_setbyuser = 1 ;
       set_2Dhist_hbin( hist_nbin ) ;
       iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-clbin") == 0 ){   /* SECRET OPTION - 08 May 2007 */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       hist_mode  = GA_HIST_CLEQWD ;
       hist_param = (float)strtod(argv[iarg],NULL) ; hist_setbyuser = 1 ;
       iarg++ ; continue ;
     }

#if 0
     if( strcmp(argv[iarg],"-izz") == 0 ){    /* EXPERIMENTAL!! */
       THD_correlate_ignore_zerozero(1) ; iarg++ ; continue ;
     }
#endif

     if( strcmp(argv[iarg],"-eqbin") == 0 ){   /* SECRET OPTION - 08 May 2007 */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       hist_mode  = GA_HIST_EQHIGH ;
       hist_param = (float)strtod(argv[iarg],NULL) ; hist_setbyuser = 1 ;
       if( hist_param < 3.0f || hist_param > 255.0f ){
         WARNING_message("'-eqbin %f' is illegal -- ignoring :-(",hist_param) ;
         hist_mode = 0 ; hist_param = 0.0f ; hist_setbyuser = 0 ;
       }
       iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-wtmrad") == 0 ){   /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       wt_medsmooth = (float)strtod(argv[iarg],NULL) ;
       iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-wtgrad") == 0 ){   /* SECRET OPTION */
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       wt_gausmooth = (float)strtod(argv[iarg],NULL) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-verb") == 0 ){
       verb++ ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-VERB") == 0 ){
       verb+=2 ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-quiet") == 0 ){  /* 10 Oct 2006 */
       verb=0 ; iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-usetemp") == 0 ){  /* 20 Dec 2006 */
       usetemp = 1 ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-nousetemp") == 0 ){
       usetemp = 0 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-floatize",6) == 0 ){
       floatize++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-nopad",5) == 0 ){
       zeropad = 0 ; iarg++ ; continue ;
     }

     /*----- Check the various cost options -----*/

     jj = meth_name_to_code( argv[iarg]+1 ) ; /* check for match after the '-' */
     if( jj > 0 ){
       meth_code = jj ; iarg++ ; continue ;   /* there was a match */
     }

     /** -cost shortname  *OR*  -cost longname **/

     if( strcmp(argv[iarg],"-cost") == 0 || strcmp(argv[iarg],"-meth") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '-cost' :-(") ;

       jj = meth_name_to_code( argv[iarg] ) ;
       if( jj > 0 ){ meth_code = jj ; iarg++ ; continue ; }

       /* fail here, UNLESS the method is 'lpc+something' */

       if( ! (strlen(argv[iarg]) > 5 &&
               (   strncasecmp(argv[iarg],"lpc+",4) == 0
                || strncasecmp(argv[iarg],"lpa+",4) == 0 ) ) )
         ERROR_exit("Unknown code '%s' after -cost :-(",argv[iarg]) ;

       /* fall through to lpc+ code */

       micho_fallthru = 1 ;
     }

     /* 24 Feb 2010: special option for -lpc+stuff or -lpa+stuff */

     if( micho_fallthru ||
         (strlen(argv[iarg]) > 6 &&
           (   strncasecmp(argv[iarg],"-lpc+",5) == 0
            || strncasecmp(argv[iarg],"-lpa+",5) == 0 ) ) ){
       char *cpt ;
       if( strcasestr(argv[iarg],"lpc") != NULL )
         meth_code = GA_MATCH_LPC_MICHO_SCALAR ;
       else if( strcasestr(argv[iarg],"lpa") != NULL )
         meth_code = GA_MATCH_LPA_MICHO_SCALAR ;
       else {
         WARNING_message("How did this happen? argv[%d] = %s",iarg,argv[iarg]) ;
         meth_code = GA_MATCH_LPC_MICHO_SCALAR ;
       }
       micho_fallthru = 0 ;
       cpt = strcasestr(argv[iarg],"+hel*"); if( cpt != NULL ) micho_hel = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+hel:"); if( cpt != NULL ) micho_hel = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+mi*" ); if( cpt != NULL ) micho_mi  = strtod(cpt+4,NULL);
       cpt = strcasestr(argv[iarg],"+mi:" ); if( cpt != NULL ) micho_mi  = strtod(cpt+4,NULL);
       cpt = strcasestr(argv[iarg],"+nmi*"); if( cpt != NULL ) micho_nmi = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+nmi:"); if( cpt != NULL ) micho_nmi = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+crA*"); if( cpt != NULL ) micho_crA = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+crA:"); if( cpt != NULL ) micho_crA = strtod(cpt+5,NULL);
       cpt = strcasestr(argv[iarg],"+ov*" ); if( cpt != NULL ) micho_ov  = strtod(cpt+4,NULL);
       cpt = strcasestr(argv[iarg],"+ov:" ); if( cpt != NULL ) micho_ov  = strtod(cpt+4,NULL);
       cpt = strcasestr(argv[iarg],"ZZ")   ; micho_zfinal = (cpt != NULL) ;

       INFO_message("%s parameters: hel=%.2f mi=%.2f nmi=%.2f crA=%.2f ov=%.2f %s",
                    meth_shortname[meth_code-1] ,
                    micho_hel , micho_mi , micho_nmi , micho_crA , micho_ov ,
                    micho_zfinal ? "[to be zeroed at Final iteration]" : "\0" ) ;
       iarg++ ; continue ;
     }

#ifdef ALLOW_METH_CHECK
     /*----- -check costname -----*/

     if( strncasecmp(argv[iarg],"-check",5) == 0 ){
#if 0
       if( strncmp(argv[iarg],"-CHECK",5) == 0 ) meth_median_replace = 1 ; /* not good */
#endif
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;

       for( ; iarg < argc && argv[iarg][0] != '-' ; iarg++ ){
         if( meth_check_count == NMETH ) continue ; /* malicious user? */
         jj = meth_name_to_code(argv[iarg]) ;
         if( jj > 0 ){ meth_check[meth_check_count++] = jj; continue; }
         WARNING_message("Unknown code '%s' after -check :-(",argv[iarg]) ;
       }
       continue ;
     }
#else
     if( strncasecmp(argv[iarg],"-check",5) == 0 ){
       WARNING_message("option '%s' is no longer available",argv[iarg]) ;
       for( ; iarg < argc && argv[iarg][0] != '-' ; iarg++ ) ; /*nada*/
       continue ;
     }
#endif

     /*-----*/

     if( strcmp(argv[iarg],"-emask") == 0 ){                   /* 14 Feb 2013 */
       if( emask != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '-emask' :-(") ;
       emask = THD_create_mask_from_string( argv[iarg] ) ;
       if( emask == NULL ) ERROR_exit("Can't create emask from '%s'",argv[iarg]) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-base") == 0 ){
       if( dset_base != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '-base' :-(") ;
       dset_base = THD_open_dataset( argv[iarg] ) ;
       if( dset_base == NULL ) ERROR_exit("can't open -base dataset '%s' :-(",argv[iarg]);
       ii = (int)DSET_BRICK_TYPE(dset_base,0) ;
       if( ii != MRI_float && ii != MRI_short && ii != MRI_byte )
#if 0
         ERROR_exit("base dataset %s has non-scalar data type '%s' :-(",
                    DSET_BRIKNAME(dset_base) , MRI_TYPE_name[ii] ) ;
#else
         WARNING_message("base dataset %s has non-scalar data type '%s' :-(",
                    DSET_BRIKNAME(dset_base) , MRI_TYPE_name[ii] ) ;
#endif
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-source",6) == 0 ||
         strncmp(argv[iarg],"-input" ,5) == 0 ||
         strncmp(argv[iarg],"-target",7) == 0 ||
         strncmp(argv[iarg],"-src"   ,4) == 0   ){
       if( dset_targ != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       dset_targ = THD_open_dataset( argv[iarg] ) ;
       if( dset_targ == NULL )
         ERROR_exit("can't open -%s dataset '%s' :-(",argv[iarg-1],argv[iarg]);
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-median",5) == 0 ){        /* SECRET OPTION */
       sm_code = GA_SMOOTH_MEDIAN ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-twoblur",7) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       sm_rad = (float)strtod(argv[iarg],NULL) ; twopass = 1 ;
       if( sm_rad < 0.0f ) sm_rad = 0.0f ;
       iarg++ ; continue ;
     }

     if( strncmp(argv[iarg],"-fineblur",8) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       fine_rad = (float)strtod(argv[iarg],NULL) ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-twobest",7) == 0 ){
       static int first=1 ; int tbold=tbest ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       tbest = (int)strtod(argv[iarg],NULL) ; twopass = 1 ;
       if( tbest < 0 ){
         WARNING_message("-twobest %d is illegal: replacing with 0",tbest) ;
         tbest = 0 ;
       } else if( tbest > PARAM_MAXTRIAL ){
         INFO_message("-twobest %d is too big: replaced with %d",tbest,PARAM_MAXTRIAL) ;
         tbest = PARAM_MAXTRIAL ;
       } else if( !first && tbold > tbest ){
         INFO_message("keeping older/larger -twobest value of %d",tbold) ;
         tbest = tbold ;
       }
       first = 0 ; iarg++ ; continue ;
     }

     if( strncmp(argv[iarg],"-num_rtb",7) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       num_rtb = (int)strtod(argv[iarg],NULL) ; twopass = 1 ;
            if( num_rtb <= 0   ) num_rtb = 0 ;
       else if( num_rtb <  66  ) num_rtb = 66 ;
       else if( num_rtb >  666 ) num_rtb = 666 ;
       iarg++ ; continue ;
     }

     if( strncmp(argv[iarg],"-nocast",6) == 0 ){
       nocast = 1 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-onepass",6) == 0 ){
       twopass = twofirst = 0 ; iarg++ ; continue ;
     }
     if( strncmp(argv[iarg],"-twopass",6) == 0 ){
       twopass = 1 ; twofirst = 0 ; iarg++ ; continue ;
     }
     if( strncmp(argv[iarg],"-twofirst",6) == 0 ){
       twofirst = twopass = 1 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-output",5) == 0 || strncmp(argv[iarg],"-prefix",5) == 0 ){
       if( prefix != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: '%s' '%s' :-(",argv[iarg-1],argv[iarg]) ;
       if( strcmp(argv[iarg],"NULL") == 0 ) prefix = NULL ;
       else                                 prefix = argv[iarg] ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-1Dfile",5) == 0 || strncmp(argv[iarg],"-1Dparam_save",12) == 0 ){
       if( param_save_1D != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]);
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: %s '%s' :-(",argv[iarg-1],argv[iarg]) ;
       if( STRING_HAS_SUFFIX(argv[iarg],".1D") ){
         param_save_1D = argv[iarg] ;
       } else {
         param_save_1D = calloc(sizeof(char*),strlen(argv[iarg])+16) ;
         strcpy(param_save_1D,argv[iarg]) ; strcat(param_save_1D,".param.1D") ;
       }
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-1Dmatrix_save",13) == 0 ){
       if( matrix_save_1D != NULL ) ERROR_exit("Can't have multiple %s options :-(",argv[iarg]);
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: %s '%s' :-(",argv[iarg-1],argv[iarg]) ;
       if( STRING_HAS_SUFFIX(argv[iarg],".1D") ){
         matrix_save_1D = argv[iarg] ;
       } else {
         matrix_save_1D = calloc(sizeof(char*),strlen(argv[iarg])+16) ;
         strcpy(matrix_save_1D,argv[iarg]) ; strcat(matrix_save_1D,".aff12.1D") ;
       }
       iarg++ ; continue ;
     }

     /*-----*/

#undef  APL
#define APL(i,j) apply_far[(i)+(j)*apply_nx] /* i=param index, j=row index */

     if( strncmp(argv[iarg],"-1Dapply",5)        == 0 ||
         strncmp(argv[iarg],"-1Dparam_apply",13) == 0   ){
       char *fname ;

       if( APPLYING )
         ERROR_exit("Can't have multiple 'apply' options :-(") ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
#if 0
       if( strncmp(argv[iarg],"1D:",3) != 0 && !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: %s '%s' :-(",argv[iarg-1],argv[iarg]) ;
#endif
       fname = argv[iarg] ;
       if( strcasecmp(fname,"IDENTITY")==0 ) fname = "1D: 12@0'" ;
       apply_1D = fname ; qim = mri_read_1D(apply_1D) ;
       if( qim == NULL ) ERROR_exit("Can't read %s '%s' :-(",argv[iarg-1],apply_1D) ;
       apply_im  = mri_transpose(qim); mri_free(qim);
       apply_far = MRI_FLOAT_PTR(apply_im) ;
       apply_nx  = apply_im->nx ;  /* # of values per row */
       apply_ny  = apply_im->ny ;  /* number of rows */
       apply_mode = APPLY_PARAM ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-1Dmatrix_apply",13) == 0 ){
       char *fname ;
       if( APPLYING )
         ERROR_exit("Can't have multiple 'apply' options :-(") ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
#if 0
       if( !THD_filename_ok(argv[iarg]) )
         ERROR_exit("badly formed filename: %s '%s' :-(",argv[iarg-1],argv[iarg]) ;
#endif
       fname = argv[iarg] ;
       if( strcasecmp(fname,"IDENTITY")==0 )
         fname = "1D: 1 0 0 0   0 1 0 0   0 0 1 0" ;
       apply_1D = fname ; qim = mri_read_1D(apply_1D) ;
       if( qim == NULL ) ERROR_exit("Can't read -1Dmatrix_apply '%s' :-(",apply_1D) ;
       apply_im  = mri_transpose(qim); mri_free(qim);
       apply_far = MRI_FLOAT_PTR(apply_im) ;
       apply_nx  = apply_im->nx ;  /* # of values per row */
       apply_ny  = apply_im->ny ;  /* number of rows */
       apply_mode = APPLY_AFF12 ;
       if( apply_nx < 12 && apply_im->nvox == 12 ){  /* special case of a 3x4 array */
         apply_nx = 12 ; apply_ny = 1 ;
         INFO_message("-1Dmatrix_apply: converting input 3x4 array to 1 row of 12 numbers") ;
       }
       if( apply_nx < 12 )
         ERROR_exit("%d = Less than 12 numbers per row in -1Dmatrix_apply '%s' :-(" ,apply_nx,apply_1D) ;
       else if( apply_nx > 12 )
         WARNING_message("%d = More than 12 numbers per row in -1Dmatrix_apply '%s'",apply_ny,apply_1D) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-NN") == 0 || strncmp(argv[iarg],"-nearest",6) == 0 ){
       interp_code = MRI_NN ; iarg++ ; got_interp = 1 ;continue ;
     }
     if( strncmp(argv[iarg],"-linear",4)==0 || strncmp(argv[iarg],"-trilinear",6)==0 ){
       interp_code = MRI_LINEAR ; iarg++ ; got_interp = 1 ;continue ;
     }
     if( strncmp(argv[iarg],"-cubic",4)==0 || strncmp(argv[iarg],"-tricubic",6)==0 ){
       interp_code = MRI_CUBIC ; iarg++ ; got_interp = 1 ;continue ;
     }
     if( strncmp(argv[iarg],"-quintic",4)==0 || strncmp(argv[iarg],"-triquintic",6)==0 ){
       interp_code = MRI_QUINTIC ; iarg++ ; got_interp = 1 ;continue ;
     }
#if 0
     if( strncasecmp(argv[iarg],"-WSINC") == 0 ){
       interp_code = MRI_WSINC5 ; iarg++ ; got_interp = 1 ;continue ;
     }
#endif
     if( strncmp(argv[iarg],"-interp",5)==0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( strcmp(argv[iarg],"NN")==0 || strncmp(argv[iarg],"nearest",5)==0 )
         interp_code = MRI_NN ;
       else
       if( strncmp(argv[iarg],"linear",3)==0 || strncmp(argv[iarg],"trilinear",5)==0 )
         interp_code = MRI_LINEAR ;
       else
       if( strncmp(argv[iarg],"cubic",3)==0 || strncmp(argv[iarg],"tricubic",5)==0 )
         interp_code = MRI_CUBIC ;
       else
       if( strncmp(argv[iarg],"quintic",3)==0 || strncmp(argv[iarg],"triquintic",5)==0 )
         interp_code = MRI_QUINTIC ;
#if 0
       else
       if( strncasecmp(argv[iarg],"WSINC",5)==0 )
         interp_code = MRI_WSINC5 ;
#endif
       else
         ERROR_exit("Unknown code '%s' after '%s' :-(",argv[iarg],argv[iarg-1]) ;
       iarg++ ; got_interp = 1 ;continue ;
     }

     if( strncmp(argv[iarg],"-final",5) == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( strcmp(argv[iarg],"NN") == 0 || strncmp(argv[iarg],"nearest",5) == 0 )
         final_interp = MRI_NN ;
       else
       if( strncmp(argv[iarg],"linear",3) == 0 || strncmp(argv[iarg],"trilinear",5) == 0 )
         final_interp = MRI_LINEAR ;
       else
       if( strncmp(argv[iarg],"cubic",3) == 0 || strncmp(argv[iarg],"tricubic",5) == 0 )
         final_interp = MRI_CUBIC ;
       else
       if( strncmp(argv[iarg],"quintic",3)==0 || strncmp(argv[iarg],"triquintic",5)==0 )
         final_interp = MRI_QUINTIC ;
       else
       if( strncasecmp(argv[iarg],"WSINC",5)==0 )
         final_interp = MRI_WSINC5 ;
       else
         ERROR_exit("Unknown code '%s' after '%s' :-(",argv[iarg],argv[iarg-1]) ;
       iarg++ ; got_final = 1 ;continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-converge",5) == 0 ){
       float vv ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       vv = (float)strtod(argv[iarg],NULL) ;
            if( vv < 0.0001f ){ vv = 0.0001f; WARNING_message("%s: limited %s to 0.0001",argv[iarg-1],argv[iarg]); }
       else if( vv > 6.666f  ){ vv = 6.666f ; WARNING_message("%s: limited %s to 6.666" ,argv[iarg-1],argv[iarg]); }
       conv_mm = vv ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-nmatch",5) == 0 ){
       char *cpt ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       npt_match = (int)strtod(argv[iarg],&cpt) ;
       if( npt_match <= 0 )
         ERROR_exit("Illegal value '%s' after '%s' :-(",argv[iarg],argv[iarg-1]) ;
       if( *cpt == '%' || npt_match <= 100 )
         npt_match = -npt_match ;  /* signal for % */
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-warp") == 0 ){
      if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
      if( strcmp(argv[iarg],"sho")     ==0 || strcmp(argv[iarg],"shift_only")        ==0 )
        warp_code = WARP_SHIFT ;
      else if( strcmp(argv[iarg],"shr")==0 || strcmp(argv[iarg],"shift_rotate")      ==0 )
        warp_code = WARP_ROTATE ;
      else if( strcmp(argv[iarg],"srs")==0 || strcmp(argv[iarg],"shift_rotate_scale")==0 )
        warp_code = WARP_SCALE ;
      else if( strcmp(argv[iarg],"aff")==0 || strcmp(argv[iarg],"affine_general")    ==0 )
        warp_code = WARP_AFFINE ;
      else
        ERROR_exit("Unknown code '%s' after '%s' :-(",argv[iarg],argv[iarg-1]) ;
      iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-dof") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       ii = (int)strtod(argv[iarg],NULL) ;
       switch(ii){
         case  3: warp_code = WARP_SHIFT  ; break ;
         case  6: warp_code = WARP_ROTATE ; break ;
         case  9: warp_code = WARP_SCALE  ; break ;
         case 12: warp_code = WARP_AFFINE ; break ;
         default:
           ERROR_exit("Unknown value '%s' after '%s' :-(",argv[iarg],argv[iarg-1]) ;
       }
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-parfix") == 0 ){
       if( ++iarg >= argc-1 ) ERROR_exit("need 2 arguments after '%s' :-(",argv[iarg-1]) ;
       if( nparopt >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       ii = (int)strtod(argv[iarg],NULL) ;
       if( ii <= 0 ) ERROR_exit("-parfix '%s' is illegal :-(",argv[iarg]) ;
       v1 = (float)strtod(argv[++iarg],NULL) ;
       paropt[nparopt].np   = ii-1 ;
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = v1 ;
       nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-parang") == 0 ){
       if( ++iarg >= argc-2 ) ERROR_exit("need 3 arguments after '%s' :-(",argv[iarg-1]) ;
       if( nparopt >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       ii = (int)strtod(argv[iarg],NULL) ;
       if( ii <= 0 ) ERROR_exit("-parang '%s' is illegal :-(",argv[iarg]) ;
       v1 = (float)strtod(argv[++iarg],NULL) ;
       v2 = (float)strtod(argv[++iarg],NULL) ;
       if( v1 > v2 ) ERROR_exit("-parang %d '%s' '%s' is illegal :-(",
                     ii,argv[iarg-1],argv[iarg] ) ;
       paropt[nparopt].np   = ii-1 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = v1 ;
       paropt[nparopt].vt   = v2 ;
       nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-maxrot") == 0 ){
       float vv ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( nparopt+2 >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       vv = (float)strtod(argv[iarg],NULL) ;
       if( vv <= 0.0f || vv > 90.0f ) ERROR_exit("-maxrot %f is illegal :-(",vv) ;
       paropt[nparopt].np   = 3 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 4 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 5 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-maxscl") == 0 ){
       float vv , vvi ; char *cpt ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( nparopt+2 >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       vv = (float)strtod(argv[iarg],&cpt) ;
       if( *cpt == '%' ) vv = 1.0f + 0.01*vv ;
       if( vv == 1.0f || vv > 2.0f || vv < 0.5f )
         ERROR_exit("-maxscl %f is illegal :-(",vv) ;
       if( vv > 1.0f ){ vvi = 1.0f/vv; }
       else           { vvi = vv ; vv = 1.0f/vvi ; }
       paropt[nparopt].np   = 6 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = vvi ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 7 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = vvi ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 8 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = vvi ;
       paropt[nparopt].vt   =  vv ; nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-maxshr") == 0 ){  /* 03 Dec 2010 */
       float vv ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( nparopt+2 >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       vv = (float)strtod(argv[iarg],NULL) ;
       if( vv <= 0.0f || vv > 1.0f ) ERROR_exit("-maxshr %f is illegal :-(",vv) ;
       paropt[nparopt].np   = 9 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 10 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 11 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-maxshf") == 0 ){
       float vv ;
       if( ++iarg >= argc ) ERROR_exit("no argument after '%s' :-(",argv[iarg-1]) ;
       if( nparopt+2 >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       vv = (float)strtod(argv[iarg],NULL) ;
       if( vv <= 0.0f ) ERROR_exit("-maxshf %f is illegal :-(",vv) ;
       paropt[nparopt].np   = 0 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 1 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ;
       paropt[nparopt].np   = 2 ;
       paropt[nparopt].code = PARC_RAN ;
       paropt[nparopt].vb   = -vv ;
       paropt[nparopt].vt   =  vv ; nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-parini") == 0 ){
       if( ++iarg >= argc-1 ) ERROR_exit("need 2 arguments after '%s' :-(",argv[iarg-1]) ;
       if( nparopt >= MAXPAR ) ERROR_exit("too many -par... options :-(") ;
       ii = (int)strtod(argv[iarg],NULL) ;
       if( ii <= 0 ) ERROR_exit("-parini '%s' is illegal :-(",argv[iarg]) ;
       v1 = (float)strtod(argv[++iarg],NULL) ;
       paropt[nparopt].np   = ii-1 ;
       paropt[nparopt].code = PARC_INI ;
       paropt[nparopt].vb   = v1 ;
       nparopt++ ; iarg++ ; continue ;
     }

     /*-----*/

     if( strncmp(argv[iarg],"-FPS",4)==0 || strncmp(argv[iarg],"-EPI",4)==0 ){
       int fe=-1 , pe=-1 , se=-1 ; char *fps , *aaa=argv[iarg] ;

       if( epi_targ >= 0 )
         ERROR_exit("Can't have multiple '%4.4s' options :-(",aaa) ;

       /* is the EPI dataset the target (default) or base? */

       epi_targ = (aaa[4] != '\0' && toupper(aaa[4]) == 'B') ? 0 : 1 ;

       if( aaa[1] == 'F' ){   /* -FPS code */
         if( ++iarg >= argc )  ERROR_exit("no argument after '%s' :-(",argv[iarg-1]);
         fps = argv[iarg] ;
         if( strlen(fps) < 3 ) ERROR_exit("Too short %4.4s codes '%s' :-(",aaa,fps);
       } else {
         fps = "123" ;        /* -EPI */
       }

       /* decode the FPS directions, so that
            epi_fe = freq encode direction = 0 or 1 or 2
            epi_pe = phase encode direction
            epi_se = slice encode direction */

       switch( fps[0] ){
         default: ERROR_exit("Illegal %4.4s f code '%c' :-(" , aaa,fps[0] );
         case 'i': case 'I': case 'x': case 'X': case '1':  fe = 1; break;
         case 'j': case 'J': case 'y': case 'Y': case '2':  fe = 2; break;
         case 'k': case 'K': case 'z': case 'Z': case '3':  fe = 3; break;
       }
       switch( fps[1] ){
         default: ERROR_exit("Illegal %4.4s p code '%c' :-(" , aaa,fps[1] );
         case 'i': case 'I': case 'x': case 'X': case '1':  pe = 1; break;
         case 'j': case 'J': case 'y': case 'Y': case '2':  pe = 2; break;
         case 'k': case 'K': case 'z': case 'Z': case '3':  pe = 3; break;
       }
       switch( fps[2] ){
         default: ERROR_exit("Illegal %4.4s s code '%c' :-(" , aaa,fps[2] );
         case 'i': case 'I': case 'x': case 'X': case '1':  se = 1; break;
         case 'j': case 'J': case 'y': case 'Y': case '2':  se = 2; break;
         case 'k': case 'K': case 'z': case 'Z': case '3':  se = 3; break;
       }
       if( fe+pe+se != 6 ) ERROR_exit("Illegal %4.4s combination '%s' :-(",aaa,fps);

       epi_fe = fe-1 ; epi_pe = pe-1 ; epi_se = se-1 ;  /* process later */

       if( verb > 1 )
         INFO_message("EPI parameters: targ=%d  fe=%d pe=%d se=%d",
                      epi_targ,epi_fe,epi_pe,epi_se ) ;

       /* restrict some transformation parameters */

       smat = SMAT_YYY ;               /* shear only in y (PE) direction */
       warp_freeze = 1 ;               /* 10 Oct 2006 */

       /* matrix order depends on if we are restricting transformation
          parameters in the base image or in the target image coordinates */

       matorder = (epi_targ) ? MATORDER_SDU : MATORDER_USD ;

       paropt[nparopt].np   = 6 ;      /* fix x-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 8 ;      /* fix z-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 11 ;      /* fix last shear to 0 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 0.0 ; nparopt++ ;

       twofirst = 1; replace_base = 1;
#if 0
       replace_meth = GA_MATCH_PEARSON_SCALAR;
#endif
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-replacebase") == 0 ){  /* 18 Oct 2006 */
       twofirst = replace_base = 1 ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-warpfreeze") == 0 ){  /* 18 Oct 2006 */
       warp_freeze = 1 ; iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-nowarpfreeze") == 0 ){  /* 01 Feb 2007 */
       warp_freeze = 0 ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-noreplacebase") == 0 ){  /* 01 Feb 2007 */
       replace_base = 0 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-replacemeth") == 0 ){  /* 18 Oct 2006 */
       if( ++iarg >= argc ) ERROR_exit("no argument after '-replacemeth' :-(") ;

       if( strcmp(argv[iarg],"0") == 0 ){
         replace_meth = 0 ; iarg++ ; continue ;  /* special case */
       }

       jj = meth_name_to_code(argv[iarg]) ;
       if( jj > 0 ){ replace_meth = jj ; iarg++ ; continue ; }

       ERROR_exit("Unknown code '%s' after -replacemeth :-(",argv[iarg]) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-Xwarp") == 0 ){  /* 02 Oct 2006 */
       if( XYZ_warp > 0 ) ERROR_exit("only one use of -[XYZ]warp is allowed :-(");
       matorder = MATORDER_USD ;       /* rotation after shear and scale */

       paropt[nparopt].np   = 7 ;      /* fix y-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 8 ;      /* fix z-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 11 ;      /* fix last shear to 0 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 0.0 ; nparopt++ ;

       smat = SMAT_XXX ;                /* fix shear matrix to x-only */
       XYZ_warp = 1 ;iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-Ywarp") == 0 ){  /* 02 Oct 2006 */
       if( XYZ_warp > 0 ) ERROR_exit("only one use of -[XYZ]warp is allowed :-(");
       matorder = MATORDER_USD ;       /* rotation after shear and scale */

       paropt[nparopt].np   = 6 ;      /* fix x-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 8 ;      /* fix z-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 11 ;      /* fix last shear to 0 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 0.0 ; nparopt++ ;

       smat = SMAT_YYY ;                /* fix shear matrix to y-only */
       XYZ_warp = 2 ; iarg++ ; continue ;
     }

     if( strcmp(argv[iarg],"-Zwarp") == 0 ){  /* 02 Oct 2006 */
       if( XYZ_warp > 0 ) ERROR_exit("only one use of -[XYZ]warp is allowed :-(");
       matorder = MATORDER_USD ;       /* rotation after shear and scale */

       paropt[nparopt].np   = 6 ;      /* fix x-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 7 ;      /* fix y-scale to 1 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 1.0 ; nparopt++ ;

       paropt[nparopt].np   = 11 ;      /* fix last shear to 0 */
       paropt[nparopt].code = PARC_FIX ;
       paropt[nparopt].vb   = 0.0 ; nparopt++ ;

       smat = SMAT_ZZZ ;                /* fix shear matrix to x-only */
       XYZ_warp = 3 ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-SDU") == 0 ){
       matorder = MATORDER_SDU ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-SUD") == 0 ){
       matorder = MATORDER_SUD ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-DSU") == 0 ){
       matorder = MATORDER_DSU ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-DUS") == 0 ){
       matorder = MATORDER_DUS ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-USD") == 0 ){
       matorder = MATORDER_USD ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-UDS") == 0 ){
       matorder = MATORDER_UDS ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-ashift") == 0 ){
       dcode = DELTA_AFTER     ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-bshift") == 0 ){
       dcode = DELTA_BEFORE    ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-Slower") == 0 ){
       smat  = SMAT_LOWER      ; iarg++ ; continue ;
     }
     if( strcmp(argv[iarg],"-Supper") == 0 ){
       smat  = SMAT_UPPER      ; iarg++ ; continue ;
     }

     /*-----*/

     if( strcasecmp(argv[iarg],"-Lunch") == 0 ){  /* 23 Sep 2020 */
       WARNING_message("There is no free '%s' with AFNI :(",argv[iarg]) ;
       iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-Faster") == 0 ){
       WARNING_message("You want '%s'? We already use OpenMP! Give me a break.",argv[iarg]) ;
       iarg++ ; continue ;
     }

     /*-----*/

     if( strcmp(argv[iarg],"-blok") == 0 ){
       int ia=0 ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after -blok :-(") ;
       if( strncmp(argv[iarg],"SPHERE(",7) == 0 ){
         ia = 7 ; bloktype = GA_BLOK_BALL ;
       } else if( strncmp(argv[iarg],"BALL(",5) == 0 ){
         ia = 5 ; bloktype = GA_BLOK_BALL ;
       } else if( strncmp(argv[iarg],"RECT(",5) == 0 ){
         ia = 5 ; bloktype = GA_BLOK_CUBE ;
       } else if( strncmp(argv[iarg],"CUBE(",5) == 0 ){
         ia = 5 ; bloktype = GA_BLOK_CUBE ;
       } else if( strncmp(argv[iarg],"RHDD(",5) == 0 ){
         ia = 5 ; bloktype = GA_BLOK_RHDD ;
       } else if( strncmp(argv[iarg],"TOHD(",5) == 0 ){
         ia = 5 ; bloktype = GA_BLOK_TOHD ;
       } else {
         ERROR_exit("Illegal argument after -blok :-(") ;
       }
       blokrad = (float)strtod(argv[iarg]+ia,NULL) ;
       iarg++ ; continue ;
     }

     /*-----*/

     ERROR_message("Unknown and Illegal option '%s' :-( :-( :-(",argv[iarg]) ;
     suggest_best_prog_option(argv[0], argv[iarg]);
     exit(1);

   } /*---- end of loop over command line args ----*/

   if( iarg < argc )  /* oopsie */
     WARNING_message("Processing command line options stopped at '%s'",argv[iarg]);

   /*---------------------------------------------------------------*/
   /*--- check inputs for validity, consistency, and moral fibre ---*/

   /* set random seed */
   if( seed == 0 ) seed = (long)time(NULL)+(long)getpid() ;
   srand48(seed) ;

   /* ls/lpc/lpa: turn -autoweight on unless it was forced off. */
   /* [changed from pure warning to current status 23 Jan 2017] */

   if( !wtspecified &&
       ( meth_code == GA_MATCH_PEARSON_SCALAR   ||
         meth_code == GA_MATCH_PEARSON_LOCALS   ||
         meth_code == GA_MATCH_PEARSON_LOCALA   ||
         meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
         meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) ){
     auto_weight = 1 ;
     WARNING_message(
       "Cost 'ls' or 'lpc' or 'lpa' ==> turning '-autoweight' on\n"
       "          If you DO NOT want this to happen, then use one\n"
       "          of '-autobox' or '-automask' or '-noauto'.\n"     ) ;
   }

   if( im_tmask == NULL && !auto_tmask &&
       ( meth_code == GA_MATCH_PEARSON_LOCALS   ||
         meth_code == GA_MATCH_PEARSON_LOCALA   ||
         meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
         meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) ){
       WARNING_message(
        "'-source_automask' is strongly recommended when using -lpc or -lpa") ;
   }

   if( doing_2D &&
       ( meth_code == GA_MATCH_PEARSON_LOCALS   ||
         meth_code == GA_MATCH_PEARSON_LOCALA   ||
         meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
         meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) ){
     WARNING_message(
      "-lpc or -lpa cost functionals do NOT work well with 2D images :(") ;
   }

   /* set histogram mode (for computing -hel, -mi, -cr, etc) */

   if( !hist_setbyuser ){   /* 25 Jul 2007 */
     switch( meth_code ){
       case GA_MATCH_PEARSON_LOCALS:
       case GA_MATCH_PEARSON_LOCALA:
       case GA_MATCH_SPEARMAN_SCALAR:
       case GA_MATCH_PEARSON_SCALAR:
         hist_mode = (do_allcost || meth_check_count) ? GA_HIST_CLEQWD : 0 ;
       break ;

       default:
         hist_mode  = GA_HIST_CLEQWD ;
       break ;
     }
   }

   /* if the user wants to see all cost functional values */

   if( do_allcost < 0 && prefix != NULL ){  /* 19 Sep 2007 */
     prefix = NULL ;
     WARNING_message("-allcostX means -prefix is ignored :-(") ;
   }
   if( do_allcost < 0 && param_save_1D != NULL ){
     param_save_1D = NULL ;
     WARNING_message("-allcostX means -1Dparam_save is ignored :-(") ;
   }
   if( do_allcost < 0 && matrix_save_1D != NULL ){
     matrix_save_1D = NULL ;
     WARNING_message("-allcostX means -1Dmatrix_save is ignored :-(") ;
   }

   /* I don't think anyone uses this option */

   if( warp_freeze ) twofirst = 1 ;  /* 10 Oct 2006 */

   /* applying an input transformation from -nwarp: obsolescent code */

   if( apply_mode > 0 && nwarp_pass ){
     switch( nwarp_type ){
       default: ERROR_exit("Can't apply that nonlinear warp :-(  [%d]",nwarp_type) ;

       case WARP_BILINEAR:{
         if( apply_nx == NPBIL+4 ){
           apply_mode = APPLY_BILIN ;
           INFO_message(
            "found %d param/row in param file '%s'; applying bilinear warp",
            apply_nx , apply_1D) ;
         } else {
           ERROR_exit(
            "found %d param/row in param file '%s'; not right for bilinear warp",
            apply_nx , apply_1D) ;
         }
       }
       break ;

       case WARP_CUBIC:{
         if( apply_nx == NPCUB+4 ){
           apply_mode = APPLY_CUBIC ;
           INFO_message(
            "found %d param/row in param file '%s'; applying cubic/poly3 warp",
            apply_nx , apply_1D) ;
         } else {
           ERROR_exit(
            "found %d param/row in param file '%s'; not right for cubic/poly3 warp",
            apply_nx , apply_1D) ;
         }
       }
       break ;

       case WARP_QUINT:{
         if( apply_nx == NPQUINT+4 ){
           apply_mode = APPLY_QUINT ;
           INFO_message(
            "found %d param/row in param file '%s'; applying quintic/poly5 warp",
            apply_nx , apply_1D) ;
         } else {
           ERROR_exit(
            "found %d param/row in param file '%s'; not right for quintic/poly5 warp",
            apply_nx , apply_1D) ;
         }
       }
       break ;

       case WARP_HEPT:{
         if( apply_nx == NPHEPT+4 ){
           apply_mode = APPLY_HEPT ;
           INFO_message(
            "found %d param/row in param file '%s'; applying heptic/poly7 warp",
            apply_nx , apply_1D) ;
         } else {
           ERROR_exit(
            "found %d param/row in param file '%s'; not right for heptic/poly7 warp",
            apply_nx , apply_1D) ;
         }
       }
       break ;

       case WARP_NONI:{
         if( apply_nx == NPNONI+4 ){
           apply_mode = APPLY_NONI ;
           INFO_message(
            "found %d param/row in param file '%s'; applying nonic/poly9 warp",
            apply_nx , apply_1D) ;
         } else {
           ERROR_exit(
            "found %d param/row in param file '%s'; not right for nonic/poly9 warp",
            apply_nx , apply_1D) ;
         }
       }
       break ;
     } /* end of switch on nwarp_type */
   }

   if( nwarp_pass && meth_check_count > 0 ){  /* 15 Dec 2010 */
     meth_check_count = 0 ;
     if( verb ) WARNING_message("-check disabled because of -nwarp") ;
   }

   /** open target/source from last argument, if not already open **/

   if( dset_targ == NULL ){
     if( iarg >= argc )
       ERROR_exit("no source datset on command line!?") ;
     dset_targ = THD_open_dataset( argv[iarg] ) ;
     if( dset_targ == NULL )
       ERROR_exit("Can't open source dataset '%s'",argv[iarg]) ;
   }

   /* speak to the user? */

   if( verb ){
     INFO_message("Source dataset: %s",DSET_HEADNAME(dset_targ)) ;
     INFO_message("Base dataset:   %s",
                  (dset_base != NULL) ? DSET_HEADNAME(dset_base) : "(not given)" ) ;
   }

   if( nwarp_pass && DSET_NVALS(dset_targ) > 1 )
     ERROR_exit("Can't use -nwarp on more than 1 sub-brick :-(") ;

   if( nwarp_save_prefix != NULL && !nwarp_pass ){
     WARNING_message("Can't use -nwarp_save without -nwarp! :-(") ;
     nwarp_save_prefix = NULL ;
   }

   switch( tb_mast ){                        /* 19 Jul 2007 */
     case 1: dset_mast = dset_targ ; break ;
     case 2: dset_mast = dset_base ; break ;
   }

   if( replace_base && DSET_NVALS(dset_targ) == 1 ) replace_base = 0 ;

   /** check target data type **/

   targ_kind = (int)DSET_BRICK_TYPE(dset_targ,0) ;
   if( targ_kind != MRI_float && targ_kind != MRI_short && targ_kind != MRI_byte ){
#if 0
     ERROR_exit("source dataset %s has non-scalar data type '%s'",
                DSET_BRIKNAME(dset_targ) , MRI_TYPE_name[targ_kind] ) ;
#else
     WARNING_message("source dataset %s has non-scalar data type '%s'",
                DSET_BRIKNAME(dset_targ) , MRI_TYPE_name[targ_kind] ) ;
     targ_kind = MRI_float ; /* for allineation purposes */
     targ_was_vector = !floatize && ISVECTIM(DSET_BRICK(dset_targ,0)) ;
     if( targ_was_vector ) targ_vector_kind = (int)DSET_BRICK_TYPE(dset_targ,0) ;
#endif
   }
   if( !DSET_datum_constant(dset_targ) )
     WARNING_message("source dataset %s does not have constant data type :-(",
                     DSET_BRIKNAME(dset_targ)) ;

   /*-- if applying a set of parameters, some options are turned off --*/

   if( apply_1D != NULL ){
     if( prefix == NULL ) ERROR_exit("-1D*_apply also needs -prefix :-(") ;
     if( param_save_1D  != NULL ) WARNING_message("-1D*_apply: Can't do -1Dparam_save") ;
     if( matrix_save_1D != NULL ) WARNING_message("-1D*_apply: Can't do -1Dmatrix_save") ;
     wtprefix = param_save_1D = matrix_save_1D = NULL ;
     zeropad = 0 ; auto_weight = auto_tmask = 0 ;
     if( dset_weig != NULL ){
       INFO_message("-1D*_apply: Ignoring weight dataset") ;
       DSET_delete(dset_weig) ; dset_weig=NULL ;
     }
     if( im_tmask != NULL ){
       INFO_message("-1D*_apply: Ignoring -source_mask") ;
       mri_free(im_tmask) ; im_tmask = NULL ;
     }
     if( dset_mast == NULL && dxyz_mast == 0.0 )
       INFO_message("You might want to use '-master' when using '-1D*_apply'") ;
     if( do_allcost ){  /* 19 Sep 2007 */
       do_allcost = 0 ;
       INFO_message("-allcost option illegal with -1D*_apply") ;
     }
   }

   /** if no base input, target should have more than 1 sub-brick **/

   if( dset_base == NULL && apply_1D == NULL ){
     if( DSET_NVALS(dset_targ) == 1 )
       ERROR_exit("No base dataset AND source dataset has only 1 sub-brick") ;

     WARNING_message("No -base dataset: using sub-brick #0 of source") ;
     skip_first = 1 ;  /* don't register sub-brick #0 of targ to itself! */
   }

   /** check on interpolation codes **/

   if( final_interp < 0 ) final_interp = interp_code ;  /* not used */

   if( got_interp && !got_final && apply_mode > 0 ){
     WARNING_message("you are applying a warp, AND you gave a -interp code;\n"
                     "            BUT for applying a warp to produce a dataset, it is\n"
                     "            always -final that matters ==> setting -final to %s" ,
                     INTERP_methname(interp_code) ) ;
     final_interp = interp_code ;
   }

   if( (interp_code == MRI_NN       && final_interp != MRI_NN)       ||
       ( MRI_HIGHORDER(interp_code) && !MRI_HIGHORDER(final_interp) )  )
     WARNING_message("-interp is %s but -final is %s -- are you sure?",
                     INTERP_methname(interp_code) , INTERP_methname(final_interp) ) ;

   /*-- check if saving Pearson map is practicable [25 Jan 2021] --*/

   if( do_save_pearson_map && dset_base == NULL ){
     WARNING_message(
           "-PearSave option disabled -- you did not input a base dataset :(") ;
     do_save_pearson_map = 0 ;
   }

   /*--- load input datasets ---*/

   if( verb ) INFO_message("Loading datasets into memory") ;

   /* target MUST be present */

   DSET_load(dset_targ) ; CHECK_LOAD_ERROR(dset_targ) ;
   nx_targ = DSET_NX(dset_targ) ; dx_targ = fabsf(DSET_DX(dset_targ)) ;
   ny_targ = DSET_NY(dset_targ) ; dy_targ = fabsf(DSET_DY(dset_targ)) ;
   nz_targ = DSET_NZ(dset_targ) ; dz_targ = fabsf(DSET_DZ(dset_targ)) ;

   nxyz_targ[0] = nx_targ; nxyz_targ[1] = ny_targ; nxyz_targ[2] = nz_targ;
   dxyz_targ[0] = dx_targ; dxyz_targ[1] = dy_targ; dxyz_targ[2] = dz_targ;

   if( nx_targ < 2 || ny_targ < 2 )
     ERROR_exit("Source dataset has nx=%d ny=%d ???",nx_targ,ny_targ) ;

   /*-- 07 Aug 2007: make target automask? --*/

   if( im_tmask == NULL && apply_1D == NULL ){  /* 01 Mar 2010: (almost) always make this mask */

     byte *mmm ; int ndil=auto_tdilation ;
     mmm = THD_automask( dset_targ ) ;
     if( mmm == NULL )
       ERROR_exit("Can't make -source_automask for some reason :-(") ;
     im_tmask = mri_new_vol_empty( nx_targ,ny_targ,nz_targ , MRI_byte ) ;
     mri_fix_data_pointer( mmm , im_tmask ) ;
     if( ndil > 0 ){
       for( ii=0 ; ii < ndil ; ii++ ){
         THD_mask_dilate     ( nx_targ,ny_targ,nz_targ , mmm , 3, 2 ) ;
         THD_mask_fillin_once( nx_targ,ny_targ,nz_targ , mmm , 2 ) ;
       }
     }
     if( auto_tstring == NULL ){
       auto_tstring = (char *)malloc(sizeof(char)*32) ;
       sprintf(auto_tstring,"source_automask+%d",ndil) ;
     }
     ntmask = THD_countmask( im_tmask->nvox , mmm ) ;
     if( ntmask < 666 && auto_tmask )
       ERROR_exit("Too few (%d) voxels in %s :-(",ntmask,auto_tstring) ;
     if( verb > 1 )
       INFO_message("%d voxels in %s",ntmask,auto_tstring) ;

   } else if( im_tmask != NULL ){  /*-- check -source_mask vs. target --*/

     if( im_tmask->nx != nx_targ ||
         im_tmask->ny != ny_targ || im_tmask->nz != nz_targ )
       ERROR_exit("-source_mask and -source datasets "
                  "have different dimensions! :-(\n"
                  "Have: %d %d %d versus %d %d %d\n",
                  im_tmask->nx, im_tmask->ny , im_tmask->nz,
                  nx_targ, ny_targ, nz_targ) ;
   }

   /*-- load base dataset if defined --*/

   if( dset_base != NULL ){

     DSET_load(dset_base) ; CHECK_LOAD_ERROR(dset_base) ;
     im_base = mri_scale_to_float( DSET_BRICK_FACTOR(dset_base,0) ,
                                   DSET_BRICK(dset_base,0)         ) ;
     if( im_base == NULL )
       ERROR_exit("Cannot extract float image from base dataset :(") ;

     DSET_unload(dset_base) ;
     dx_base = fabsf(DSET_DX(dset_base)) ;
     dy_base = fabsf(DSET_DY(dset_base)) ;
     dz_base = fabsf(DSET_DZ(dset_base)) ;
     if( im_base->nx < 2 || im_base->ny < 2 )
       ERROR_exit("Base dataset has nx=%d ny=%d ???",im_base->nx,im_base->ny) ;

   } else {  /* no -base, so use target[0] as the base image */

     if( apply_mode == 0 )
       INFO_message("no -base option ==> base is #0 sub-brick of source") ;
     im_base = mri_scale_to_float( DSET_BRICK_FACTOR(dset_targ,0) ,
                                   DSET_BRICK(dset_targ,0)         ) ;
     if( im_base == NULL )
       ERROR_exit("Cannot extract float image from source dataset :(") ;
     dx_base = dx_targ; dy_base = dy_targ; dz_base = dz_targ;
     if( do_cmass && apply_mode == 0 ){   /* 30 Jul 2007 */
       INFO_message("no base dataset ==> -cmass is disabled"); do_cmass = 0;
     }

   }
   nx_base = im_base->nx ;
   ny_base = im_base->ny ; nxy_base  = nx_base *ny_base ;
   nz_base = im_base->nz ; nvox_base = nxy_base*nz_base ;

   /* 2D image registration? */

   doing_2D = (nz_base == 1) ;          /* 28 Apr 2020 */

   /* check if there is some substance to the base image */

   if( !APPLYING ){                     /* 13 Mar 2017 */
     nnz = mri_nonzero_count(im_base) ;
     if( nnz < 100 )
       ERROR_exit("3dAllineate fails :: base image has %d nonzero voxel%s (< 100)",
                  nnz , (nnz==1) ? "\0" : "s" ) ;
   }

   /* Check emask for OK-ness [14 Feb 2013] */

   if( apply_mode != 0 && emask != NULL ){
     INFO_message("-emask is ignored in apply mode") ;
     KILL_bytevec(emask) ;
   }
   if( emask != NULL && emask->nar != nvox_base )
     ERROR_exit("-emask doesn't match base dataset dimensions :-(") ;

   if( nx_base < 9 || ny_base < 9 )
     ERROR_exit("Base volume i- and/or j-axis dimension < 9") ;

   /* largest grid spacing */

   dxyz_top = dx_base ;
   dxyz_top = MAX(dxyz_top,dy_base) ; dxyz_top = MAX(dxyz_top,dz_base) ;
   dxyz_top = MAX(dxyz_top,dx_targ) ;
   dxyz_top = MAX(dxyz_top,dy_targ) ; dxyz_top = MAX(dxyz_top,dz_targ) ;

   /* crop off negative voxels in the base */

   if( do_zclip ){
     float *bar = MRI_FLOAT_PTR(im_base) ;
     for( ii=0 ; ii < nvox_base ; ii++ ) if( bar[ii] < 0.0f ) bar[ii] = 0.0f ;
   }

   /* x flip base for mirror check? [18 Jun 2019] */

   if( do_xflip_bset ){
      int nbx=im_base->nx, nby=im_base->ny, nbz=im_base->nz, nbyz = nby*nbz, ii,kk,koff,knnn ;
      float *bimar = MRI_FLOAT_PTR(im_base) , *tar ;
      tar = (float *)malloc(sizeof(float)*nbx) ;
      for( kk=0 ; kk < nbyz ; kk++ ){
        koff = kk*nbx ; knnn = koff+nbx-1 ;
        for( ii=0 ; ii < nbx ; ii++ ) tar[ii] = bimar[ii+koff] ;
        for( ii=0 ; ii < nbx ; ii++ ) bimar[knnn-ii] = tar[ii] ;
      }
      free(tar) ;
      INFO_message("3dAllineate: x-flipped base dataset") ;
   }

   /*-- find the autobbox, and setup zero-padding --*/

#undef  MPAD
#define MPAD 8
   if( zeropad ){
     float cv , *qar  ; int xpad,ypad,zpad,mpad ;
     cv = 0.33f * THD_cliplevel(im_base,0.33f) ;       /* set threshold */
     qim = mri_copy(im_base); qar = MRI_FLOAT_PTR(qim);
     for( ii=0 ; ii < qim->nvox ; ii++ ) if( qar[ii] < cv ) qar[ii] = 0.0f ;

     /* make padding depend on dataset size [22 May 2019] */

     xpad = nx_base/8; ypad = ny_base/8; zpad = nz_base/8; mpad = MPAD;
     if( mpad < xpad ) mpad = xpad ;
     if( mpad < ypad ) mpad = ypad ;
     if( mpad < zpad ) mpad = zpad ;

     /* find edges of box that contain supra-threshold contents */

     MRI_autobbox( qim, &pad_xm,&pad_xp, &pad_ym,&pad_yp, &pad_zm,&pad_zp ) ;
     mri_free(qim) ;
#if 0
     if( verb > 1 ){
       INFO_message("bbox: xbot=%3d xtop=%3d nx=%3d",pad_xm,pad_xp,nx_base);
       INFO_message("    : ybot=%3d ytop=%3d ny=%3d",pad_ym,pad_yp,ny_base);
      if( nz_base > 1 )
       INFO_message("    : zbot=%3d ztop=%3d nz=%3d",pad_zm,pad_zp,nz_base);
     }
#endif

     /* compute padding so that at least mpad all-zero slices on each face */

     pad_xm = mpad - pad_xm               ; if( pad_xm < 0 ) pad_xm = 0 ;
     pad_ym = mpad - pad_ym               ; if( pad_ym < 0 ) pad_ym = 0 ;
     pad_zm = mpad - pad_zm               ; if( pad_zm < 0 ) pad_zm = 0 ;
     pad_xp = mpad - (nx_base-1 - pad_xp) ; if( pad_xp < 0 ) pad_xp = 0 ;
     pad_yp = mpad - (ny_base-1 - pad_yp) ; if( pad_yp < 0 ) pad_yp = 0 ;
     pad_zp = mpad - (nz_base-1 - pad_zp) ; if( pad_zp < 0 ) pad_zp = 0 ;
     if( doing_2D ){ pad_zm = pad_zp = 0 ; }  /* don't z-pad 2D image! */

     zeropad = (pad_xm > 0 || pad_xp > 0 ||
                pad_ym > 0 || pad_yp > 0 || pad_zm > 0 || pad_zp > 0) ;

     if( verb > 1 && apply_mode == 0 ){
       if( zeropad ){
         if( pad_xm > 0 || pad_xp > 0 )
           INFO_message("Zero-pad: xbot=%d xtop=%d",pad_xm,pad_xp) ;
         if( pad_ym > 0 || pad_yp > 0 )
           INFO_message("Zero-pad: ybot=%d ytop=%d",pad_ym,pad_yp) ;
         if( pad_zm > 0 || pad_zp > 0 )
           INFO_message("Zero-pad: zbot=%d ztop=%d",pad_zm,pad_zp) ;
       } else {
         INFO_message("Zero-pad: not needed (plenty of internal padding)") ;
       }
     }

     /* zeropad the base image at this point in spacetime? */

     if( zeropad ){
       int nxold=nx_base , nyold=ny_base , nzold=nz_base ;
       qim = mri_zeropad_3D( pad_xm,pad_xp , pad_ym,pad_yp ,
                                             pad_zm,pad_zp , im_base ) ;
       mri_free(im_base) ; im_base = qim ;
       nx_base = im_base->nx ;
       ny_base = im_base->ny ; nxy_base  = nx_base *ny_base ;
       nz_base = im_base->nz ; nvox_base = nxy_base*nz_base ;

       if( emask != NULL ){             /* also zeropad emask [14 Feb 2013] */
         byte *ezp = (byte *)EDIT_volpad( pad_xm,pad_xp ,
                                          pad_ym,pad_yp ,
                                          pad_zm,pad_zp ,
                                          nxold,nyold,nzold ,
                                          MRI_byte , emask->ar ) ;
         if( ezp == NULL )
           ERROR_exit("zeropad of emask fails !?!") ;
         free(emask->ar) ; emask->ar = ezp ; emask->nar = nvox_base ;
       }

     }
   }

   /* dimensions of the (possibly padded) base image */

   nxyz_base[0] = nx_base; nxyz_base[1] = ny_base; nxyz_base[2] = nz_base;
   dxyz_base[0] = dx_base; dxyz_base[1] = dy_base; dxyz_base[2] = dz_base;

   { THD_3dim_dataset *qset = (dset_base != NULL) ? dset_base : dset_targ ;
     xx_code = ORIENT_xyzint[ qset->daxes->xxorient ] ;
     yy_code = ORIENT_xyzint[ qset->daxes->yyorient ] ;
     zz_code = ORIENT_xyzint[ qset->daxes->zzorient ] ;
   }

   if( doing_2D ){  /* 2D input image */
     char *tnam ;
     twodim_code = zz_code ;
     tnam = (twodim_code == 1) ? "sagittal"         /* twodim_code = slice direction */
           :(twodim_code == 2) ? "coronal"
           :(twodim_code == 3) ? "axial"
           :                     "UNKNOWABLE" ;
     if( twodim_code < 1 || twodim_code > 3 )
       ERROR_exit("2D image: orientation is %s",tnam) ;
     else if( verb )
       ININFO_message("2D image registration: orientation is %s",tnam) ;

     if( nwarp_pass ){ nwarp_fixaff = nwarp_fixmotK = nwarp_fixdepK = 1 ; }
   }

   /* set parameter freeze directions for -nwarp_fix* now [07 Dec 2010] */

   if( nwarp_pass ){
     if( twodim_code ){ nwarp_fixmotK = nwarp_fixdepK = 1 ; }  /* 2D images: no out-of-plane stuff */
     if( nwarp_fixmotI ){
       switch( xx_code ){
         case 1: nwarp_fixmotX=1;break; case 2: nwarp_fixmotY=1;break; case 3: nwarp_fixmotZ=1;break;
       }
     }
     if( nwarp_fixmotJ ){
       switch( yy_code ){
         case 1: nwarp_fixmotX=1;break; case 2: nwarp_fixmotY=1;break; case 3: nwarp_fixmotZ=1;break;
       }
     }
     if( nwarp_fixmotK ){
       switch( zz_code ){
         case 1: nwarp_fixmotX=1;break; case 2: nwarp_fixmotY=1;break; case 3: nwarp_fixmotZ=1;break;
       }
     }
     if( nwarp_fixdepI ){
       switch( xx_code ){
         case 1: nwarp_fixdepX=1;break; case 2: nwarp_fixdepY=1;break; case 3: nwarp_fixdepZ=1;break;
       }
     }
     if( nwarp_fixdepJ ){
       switch( yy_code ){
         case 1: nwarp_fixdepX=1;break; case 2: nwarp_fixdepY=1;break; case 3: nwarp_fixdepZ=1;break;
       }
     }
     if( nwarp_fixdepK ){
       switch( zz_code ){
         case 1: nwarp_fixdepX=1;break; case 2: nwarp_fixdepY=1;break; case 3: nwarp_fixdepZ=1;break;
       }
     }

     if( nwarp_fixmotX && nwarp_fixmotY && nwarp_fixmotZ )
       ERROR_exit("-nwarp_fixmot has frozen all nonlinear warping parameters :-(") ;

     if( nwarp_fixdepX && nwarp_fixdepY && nwarp_fixdepZ )
       ERROR_exit("-nwarp_fixdep has frozen all nonlinear warping parameters :-(") ;

     if( (nwarp_fixmotX || nwarp_fixmotY || nwarp_fixmotZ ||
          nwarp_fixdepX || nwarp_fixdepY || nwarp_fixdepZ   ) &&
        !NONLINEAR_IS_POLY(nwarp_type)                           )
       ERROR_exit("-nwarp_fix... cannot be used with non-polynomial -nwarp types") ;

     if( verb ){
       if( nwarp_fixmotX ) ININFO_message("-nwarp: X motions are frozen") ;
       if( nwarp_fixmotY ) ININFO_message("-nwarp: Y motions are frozen") ;
       if( nwarp_fixmotZ ) ININFO_message("-nwarp: Z motions are frozen") ;
       if( nwarp_fixdepX ) ININFO_message("-nwarp: X dependencies are frozen") ;
       if( nwarp_fixdepY ) ININFO_message("-nwarp: Y dependencies are frozen") ;
       if( nwarp_fixdepZ ) ININFO_message("-nwarp: Z dependencies are frozen") ;
     }
   }

   /* check for base:target dimensionality mismatch */

   if( !doing_2D && nz_targ == 1 )
     ERROR_exit("Can't register 2D source into 3D base :-(") ;
   if(  doing_2D && nz_targ >  1 )
     ERROR_exit("Can't register 3D source onto 2D base :-(") ;
   if( doing_2D && nwarp_pass && !NONLINEAR_IS_POLY(nwarp_type) )
     ERROR_exit("Can't use non-polynomial -nwarp on 2D images :-(") ;

   /*-- load weight dataset if defined --*/

   if( dset_weig != NULL ){

STATUS("load weight dataset") ;
     DSET_load(dset_weig) ; CHECK_LOAD_ERROR(dset_weig) ;
     im_weig = mri_scale_to_float( DSET_BRICK_FACTOR(dset_weig,0) ,
                                   DSET_BRICK(dset_weig,0)         ) ;
     DSET_unload(dset_weig) ;
     if( im_weig == NULL )
       ERROR_exit("Cannot extract float image from weight dataset :(") ;

     /* zeropad weight to match base? */

     if( zeropad ){
STATUS("zeropad weight dataset") ;
       qim = mri_zeropad_3D( pad_xm,pad_xp , pad_ym,pad_yp ,
                                             pad_zm,pad_zp , im_weig ) ;
       mri_free(im_weig) ; im_weig = qim ;
     }
     if( im_weig->nx != nx_base ||
         im_weig->ny != ny_base || im_weig->nz != nz_base )
       ERROR_exit("-weight and base volumes don't match in 3D grid dimensions :-(") ;

     /*-- convert to 0..1 range [23 Mar 2017] --*/
     { float clip=0.0f, *wf=MRI_FLOAT_PTR(im_weig); int ii,nxyz=im_weig->nvox;
       for( ii=0 ; ii < nxyz ; ii++ ) if( wf[ii] > clip ) clip = wf[ii] ;
       if( clip == 0.0f )
         ERROR_exit("Input -weight is never positive!") ;
       clip = 1.0f / clip ;
       for( ii=0 ; ii < nxyz ; ii++ ) wf[ii] *= clip ;
     }

   } else if( auto_weight ){ /* manufacture weight from base = the USUAL case */

     if( meth_noweight[meth_code-1] && auto_weight == 1 && auto_wclip == 0.0f ){
       WARNING_message("Cost function '%s' ('%s') uses -automask NOT -autoweight",
                       meth_longname[meth_code-1] , meth_shortname[meth_code-1] ) ;
       auto_weight = 2 ;
     } else if( verb > 1 ){
       INFO_message("Computing %s",auto_string) ;
     }
     if( verb > 1 ) ctim = COX_cpu_time() ;
     im_weig = mri_weightize(im_base,auto_weight,auto_dilation,auto_wclip,auto_wpow) ;
     if( verb > 1 ) INFO_message("%s net CPU time = %.1f s" ,
                                 auto_string , COX_cpu_time()-ctim ) ;
   }

   /* Apply the emask [14 Feb 2013] */

   if( emask != NULL ){
     float *war ; byte *ear=emask->ar ; int near=0 ;
     if( im_weig == NULL ){
       im_weig = mri_new_conforming(im_base,MRI_float) ;  /* all zero */
       war = MRI_FLOAT_PTR(im_weig) ;
       for( ii=0 ; ii < nvox_base ; ii++ ){
         if( ear[ii] == 0 ) war[ii] = 1.0f ; else near++ ;
       }
     } else {
       war = MRI_FLOAT_PTR(im_weig) ;
       for( ii=0 ; ii < nvox_base ; ii++ ){
         if( ear[ii] != 0 && war[ii] != 0.0f ){ war[ii] = 0.0f ; near++ ; }
       }
     }
     if( verb ) INFO_message("-emask excludes %d voxels from weight/mask",near) ;
     KILL_bytevec(emask) ;
   }

   /* also, make a mask from the weight (not used much, if at all) */

   if( im_weig != NULL ){
     float *wf = MRI_FLOAT_PTR(im_weig) ;
     byte  *mf ;
     im_mask = mri_new_conforming(im_weig,MRI_byte) ;
     mf = MRI_BYTE_PTR(im_mask) ;
     for( ii=0 ; ii < im_mask->nvox ; ii++ ) mf[ii] = (wf[ii] > 0.0f) ;
     nmask = THD_countmask(im_mask->nvox,mf) ;
     if( verb > 1 ) INFO_message("%d voxels [%.1f%%] in weight mask",
                                 nmask, 100.0*nmask/(float)im_mask->nvox ) ;
     if( !APPLYING && nmask < 100 )
       ERROR_exit("3dAllineate fails: not enough voxels in weight mask") ;

   } else {
     nmask = nvox_base ;  /* the universal 'mask' */
   }
   if( usetemp ) mri_purge(im_mask) ;

   /* save weight into a dataset? */

   if( wtprefix != NULL && im_weig != NULL ){
     THD_3dim_dataset *wset ;
     wset = EDIT_empty_copy( (dset_base!=NULL) ? dset_base : dset_targ ) ;
     EDIT_dset_items( wset ,
                        ADN_prefix    , wtprefix ,
                        ADN_nvals     , 1 ,
                        ADN_ntt       , 0 ,
                        ADN_datum_all , MRI_float ,
                      ADN_none ) ;
     EDIT_BRICK_FACTOR(wset,0,0.0);
     if( zeropad ) qim = mri_zeropad_3D( -pad_xm,-pad_xp , -pad_ym,-pad_yp ,
                                         -pad_zm,-pad_zp , im_weig ) ;
     else          qim = mri_copy(im_weig) ;
     EDIT_substitute_brick( wset, 00, MRI_float, MRI_FLOAT_PTR(qim) );
     mri_clear_data_pointer(qim) ; mri_free(qim) ;
     DSET_write(wset); if( verb ) WROTE_DSET(wset);
     DSET_delete(wset) ;
   }

   /* initialize ntask, regardless     26 Aug 2008 [rickr] */
   ntask = DSET_NVOX(dset_targ) ;
   ntask = (ntask < nmask) ? (int)sqrt(ntask*(double)nmask) : nmask ;

   /* number of points to use for matching base to target */

   if( nmask_frac < 0 ){
      if( npt_match < 0     ) npt_match = (int)(-0.01f*npt_match*ntask) ;
      if( npt_match < 9999  ) npt_match = 9999 ;
      if( npt_match > ntask ) npt_match = ntask ;
   } else {
      npt_match = (int)(nmask_frac*(double)nmask);
   }
   if( verb > 1 && apply_mode == 0 )
     INFO_message("Number of points for matching = %d",npt_match) ;

   /*------ setup alignment structure parameters ------*/

   memset(&stup,0,sizeof(GA_setup)) ;  /* NULL out */

   stup.match_code = meth_code ;
   stup.usetemp    = usetemp ;     /* 20 Dec 2006 */

   stup.hist_mode  = hist_mode ;   /* 08 May 2007 */
   stup.hist_param = hist_param ;

   /* spatial coordinates: 'cmat' transforms from ijk to xyz */

   if( !ISVALID_MAT44(dset_targ->daxes->ijk_to_dicom) )
     THD_daxes_to_mat44(dset_targ->daxes) ;
   stup.targ_cmat = DSET_CMAT(dset_targ,use_realaxes) ;

   /* base coordinates are drawn from it's header, or are same as target */

   if( dset_base != NULL ){

     float bdet , tdet ;

     if( !ISVALID_MAT44(dset_base->daxes->ijk_to_dicom) )
       THD_daxes_to_mat44(dset_base->daxes) ;
     stup.base_cmat = DSET_CMAT(dset_base,use_realaxes) ;

     /** check if handedness is the same **/

     bdet = MAT44_DET(stup.base_cmat) ; /* sign of determinant */
     tdet = MAT44_DET(stup.targ_cmat) ; /* determines handedness */
     if( bdet * tdet < 0.0f ){          /* AHA - opposite signs! */
       INFO_message("NOTE: base and source coordinate systems have different handedness") ;
       ININFO_message(
         "      Orientations: base=%s handed (%c%c%c); source=%s handed (%c%c%c)" ,
         (bdet < 0.0f) ? "Left" : "Right" ,
           ORIENT_typestr[dset_base->daxes->xxorient][0] ,
           ORIENT_typestr[dset_base->daxes->yyorient][0] ,
           ORIENT_typestr[dset_base->daxes->zzorient][0] ,
         (tdet < 0.0f) ? "Left" : "Right" ,
           ORIENT_typestr[dset_targ->daxes->xxorient][0] ,
           ORIENT_typestr[dset_targ->daxes->yyorient][0] ,
           ORIENT_typestr[dset_targ->daxes->zzorient][0]  ) ;
       ININFO_message(
         "    - It is nothing to worry about: 3dAllineate aligns based on coordinates." ) ;
       ININFO_message(
         "    - But it is always important to check the alignment visually to be sure." ) ;
     }

   } else {
     stup.base_cmat = stup.targ_cmat ;
   }

   /* for the local correlation methods:
      set the blok type (RHDD, etc), and the blok radius */

   stup.blokset = NULL ;
   if( METH_USES_BLOKS(meth_code) || do_allcost || do_save_pearson_map ){
     float mr = 1.23f * ( MAT44_COLNORM(stup.base_cmat,0)
                         +MAT44_COLNORM(stup.base_cmat,1)
                         +MAT44_COLNORM(stup.base_cmat,2) ) ;
     if( blokrad < mr ) blokrad = mr ;
     stup.bloktype = bloktype ; stup.blokrad = blokrad ; stup.blokmin = 0 ;
     if( verb ) INFO_message("Local correlation: blok type = '%s(%g)'",
                             GA_BLOK_STRING(bloktype) , blokrad        ) ;
   }

   /* setup for a matching functional that combines multiple methods */

   if( meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
       meth_code == GA_MATCH_LPA_MICHO_SCALAR   ){
     if( verb )
       INFO_message("%s parameters: hel=%.2f mi=%.2f nmi=%.2f crA=%.2f ov=%.2f %s",
                    meth_shortname[meth_code-1] ,
                    micho_hel , micho_mi , micho_nmi , micho_crA , micho_ov ,
                    micho_zfinal ? "[to be zeroed at Final iteration]" : "\0" ) ;
     GA_setup_micho( micho_hel , micho_mi , micho_nmi , micho_crA , micho_ov ) ;
   }

   /* modify base_cmat to allow for zeropad? */

   if( pad_xm > 0 || pad_ym > 0 || pad_zm > 0 )
     MAT44_EXTEND_IJK( stup.base_cmat , pad_xm,pad_ym,pad_zm ) ;

   targ_cmat = stup.targ_cmat; targ_cmat_inv = MAT44_INV(targ_cmat); /* 23 Jul 2007 */
   base_cmat = stup.base_cmat; base_cmat_inv = MAT44_INV(base_cmat);

   /*---------- define warp 'before' and 'after' matrices ----------*/

   AL_setup_warp_coords( epi_targ,epi_fe,epi_pe,epi_se,
                         nxyz_base, dxyz_base, stup.base_cmat,
                         nxyz_targ, dxyz_targ, stup.targ_cmat ) ;

   /*---------- define warp parameters and function ----------*/

   mri_genalign_affine_setup( matorder , dcode , smat ) ;

   stup.wfunc       = mri_genalign_affine ;  /* warping function */
   stup.wfunc_param = (GA_param *)calloc(12,sizeof(GA_param)) ;

   if( nwarp_pass && warp_code != WARP_AFFINE ){
     WARNING_message("Use of -nwarp ==> must allow all 12 affine parameters") ;
     warp_code = WARP_AFFINE ;
   }

   /* how many parameters will be used for affine transformation (3D) */

   switch( warp_code ){
     case WARP_SHIFT:  stup.wfunc_numpar =  3; strcpy(warp_code_string,"shift_only")        ; break;
     case WARP_ROTATE: stup.wfunc_numpar =  6; strcpy(warp_code_string,"shift_rotate")      ; break;
     case WARP_SCALE:  stup.wfunc_numpar =  9; strcpy(warp_code_string,"shift_rotate_scale"); break;
     case WARP_AFFINE: stup.wfunc_numpar = 12; strcpy(warp_code_string,"affine_general")    ; break;
   }

   /*-- check if -1Dapply_param is giving us enough parameters for this warp --*/

   if( apply_1D != NULL ){
     if( apply_mode == APPLY_PARAM && apply_nx < stup.wfunc_numpar )
       ERROR_exit(
         "-1Dparam_apply '%s': %d isn't enough parameters per row for desired warp",
         apply_1D,apply_nx);

     if( apply_ny < DSET_NVALS(dset_targ) )
       WARNING_message(
        "-1D*_apply '%s': %d isn't enough rows for source dataset -- last row will repeat",
        apply_1D,apply_ny);
   }

   /*-- macro to set up control values for a given parameter --*/

#define DEFPAR(p,nm,bb,tt,id,dd,ll)               \
 do{ stup.wfunc_param[p].min      = (bb) ;        \
     stup.wfunc_param[p].max      = (tt) ;        \
     stup.wfunc_param[p].delta    = (dd) ;        \
     stup.wfunc_param[p].toler    = (ll) ;        \
     stup.wfunc_param[p].ident    = (id) ;        \
     stup.wfunc_param[p].val_init = (id) ;        \
     stup.wfunc_param[p].val_pinit= (id) ;        \
     stup.wfunc_param[p].val_fixed= (id) ;        \
     stup.wfunc_param[p].val_out  = (id) ;        \
     strcpy( stup.wfunc_param[p].name , (nm) ) ;  \
     stup.wfunc_param[p].fixed  = 0 ;             \
 } while(0)

   /*-- compute range of shifts allowed --*/

   xxx = 0.321f * (nx_base-1) ; /* about 1/3 of base */
   yyy = 0.321f * (ny_base-1) ;
   zzz = 0.321f * (nz_base-1) ; xxx_m = yyy_m = zzz_m = 0.01f ;
   /* transform 8 corners of a cube from index space to DICOM */
   /* space, and then find largest coordinates that happen */
   for( ii=-1 ; ii <= 1 ; ii+=2 ){
    for( jj=-1 ; jj <= 1 ; jj+=2 ){
      for( kk=-1 ; kk <= 1 ; kk+=2 ){
        MAT33_VEC( base_cmat , (ii*xxx),(jj*yyy),(kk*zzz) ,
                   xxx_p,yyy_p,zzz_p ) ;
        xxx_p = fabsf(xxx_p); yyy_p = fabsf(yyy_p); zzz_p = fabsf(zzz_p);
        xxx_m = MAX(xxx_m,xxx_p);
        yyy_m = MAX(yyy_m,yyy_p); zzz_m = MAX(zzz_m,zzz_p);
   }}}
   xxx = xxx_m ; yyy = yyy_m ; zzz = zzz_m ;

   /*-- 30 Jul 2007: center-of-mass sets range of shifts --*/
   /*-- 26 Feb 2020: always compute, maybe not use       --*/

   if( 1 || do_cmass ){
     float xtarg,ytarg,ztarg , xbase,ybase,zbase ;

     mri_get_cmass_3D( im_base , &xc,&yc,&zc ) ;
     MAT44_VEC( base_cmat , xc,yc,zc , xbase,ybase,zbase ) ;
     if( verb > 1 )
       INFO_message("base center of mass = %.3f %.3f %.3f (index)",xc,yc,zc) ;
     im_targ = THD_median_brick( dset_targ ) ;
     mri_get_cmass_3D( im_targ , &xc,&yc,&zc ) ; mri_free(im_targ) ;
     if( verb > 1 )
       ININFO_message("source center of mass = %.3f %.3f %.3f (index)",xc,yc,zc) ;
     MAT44_VEC( targ_cmat , xc,yc,zc , xtarg,ytarg,ztarg ) ;
     xxc = xc = xtarg-xbase ; yyc = yc = ytarg-ybase ; zzc = zc = ztarg-zbase ;
     if( verb > 1 )
       ININFO_message("source-target CM = %.3f %.3f %.3f (xyz)",xc,yc,zc) ;
     if (do_cmass < 0) {
         /* try to figure what is OK, for partial coverage */
         if (fabs(xc) >= fabs(yc) && fabs(xc) >= fabs(zc)) {
            if (     fabs(xc) > 4.0          /* more than 4 voxels */
                  && fabs(xc) > 2.0*fabs(yc) /* more than twice the 2nd */
                  && fabs(xc) > 2.0*fabs(zc) /* more than twice the 3rd */) {
               xc = 0.0f;
            }
         } else if (fabs(yc) >= fabs(xc) && fabs(yc) >= fabs(zc)) {
            if (     fabs(yc) > 4.0          /* more than 4 voxels */
                  && fabs(yc) > 2.0*fabs(xc) /* more than twice the 2nd */
                  && fabs(yc) > 2.0*fabs(zc) /* more than twice the 3rd */) {
               yc = 0.0f;
            }
         } else if (fabs(zc) >= fabs(xc) && fabs(zc) >= fabs(yc)) {
            if (     fabs(zc) > 4.0          /* more than 4 voxels */
                  && fabs(zc) > 2.0*fabs(xc) /* more than twice the 2nd */
                  && fabs(zc) > 2.0*fabs(yc) /* more than twice the 3rd */) {
               zc = 0.0f;
            }
         }
     } else {
        if( (do_cmass & 1) == 0 ) xc = 0.0f ;
        if( (do_cmass & 2) == 0 ) yc = 0.0f ;
        if( (do_cmass & 4) == 0 ) zc = 0.0f ;
     }
     if( do_cmass && verb > 1 && apply_mode == 0 ){
       ININFO_message("estimated center of mass shifts = %.3f %.3f %.3f",xc,yc,zc) ;
     }
   } else {
     xc = yc = zc = 0.0f ;
   }

   /* WARNING message if unused cmass shifts are large compared to search range */

   if( ! do_cmass ){         /* 26 Feb 2020 */
     float rrr ;
     rrr = fabsf(xxc)/xxx ; CMbad += (rrr < 0.20f) ? 0 : (rrr < 0.5f) ? 1 : 100 ;
     rrr = fabsf(yyc)/yyy ; CMbad += (rrr < 0.20f) ? 0 : (rrr < 0.5f) ? 1 : 100 ;
     rrr = fabsf(zzc)/zzz ; CMbad += (rrr < 0.20f) ? 0 : (rrr < 0.5f) ? 1 : 100 ;
     if( CMbad > 0 && CMbad < 100 ){
       WARNING_message("center of mass shifts (-cmass) are turned off, but would be large") ;
       WARNING_message("  - at least one is more than 20%% of search range") ;
     } else if( CMbad >= 100 ){
       WARNING_message("center of mass shifts (-cmass) are turned off, but would be TERRIBLY large!") ;
       WARNING_message("  - at least one is more than 50%% of search range") ;
     }

     ININFO_message("       -cmass x y z shifts = %8.3f %8.3f %8.3f",xxc,yyc,zzc) ;
     ININFO_message(" shift search range is +/- = %8.3f %8.3f %8.3f",xxx,yyy,zzz) ;
     if( CMbad > 0 ){
       ININFO_message("                             %7.1f%% %7.1f%% %7.1f%%",
                      100.0f*fabsf(xxc)/xxx, 100.0f*fabsf(yyc)/yyy,100.0f*fabsf(zzc)/zzz  ) ;
     }

     xc = yc = zc = 0.0f ; /* pleonastic, to be safe */
   }

   /*-- smaller than normal range for parameter search? --*/

   if( do_small ){ xxx *= 0.5f ; yyy *= 0.5f ; zzz *= 0.5f ; }
   xxx_p = xc + xxx ; xxx_m = xc - xxx ;
   yyy_p = yc + yyy ; yyy_m = yc - yyy ;
   zzz_p = zc + zzz ; zzz_m = zc - zzz ;

   if( do_cmass && verb > 1 && apply_mode == 0 )
     INFO_message("shift param auto-range: %.1f..%.1f %.1f..%.1f %.1f..%.1f",
                  xxx_m,xxx_p , yyy_m,yyy_p , zzz_m,zzz_p ) ;

   /*-- we now define all 12 affine parameters, though not all may be used --*/

   /* shifts = the first 3 */

   DEFPAR( 0, "x-shift" , xxx_m , xxx_p , 0.0 , 0.0 , 0.0 ) ;    /* mm */
   DEFPAR( 1, "y-shift" , yyy_m , yyy_p , 0.0 , 0.0 , 0.0 ) ;
   DEFPAR( 2, "z-shift" , zzz_m , zzz_p , 0.0 , 0.0 , 0.0 ) ;
   if( do_cmass ){                                            /* 31 Jul 2007 */
     if( nx_base > 1 ) stup.wfunc_param[0].val_pinit = xc ;
     if( ny_base > 1 ) stup.wfunc_param[1].val_pinit = yc ;
     if( nz_base > 1 ) stup.wfunc_param[2].val_pinit = zc ;
   }

   { float rval,sval ;

     /* angles = the next 3 */

     rval = (do_small) ? 15.0f : 30.0 ;
     DEFPAR( 3, "z-angle" , -rval , rval , 0.0 , 0.0 , 0.0 ) ;  /* degrees */
     DEFPAR( 4, "x-angle" , -rval , rval , 0.0 , 0.0 , 0.0 ) ;
     DEFPAR( 5, "y-angle" , -rval , rval , 0.0 , 0.0 , 0.0 ) ;

     /* scales = the next 3 */

     rval = (do_small) ? 0.9f : 0.833f ; sval = 1.0f / rval ;
     DEFPAR( 6, "x-scale" , rval , sval , 1.0 , 0.0 , 0.0 ) ;  /* identity */
     DEFPAR( 7, "y-scale" , rval , sval , 1.0 , 0.0 , 0.0 ) ;  /*  == 1.0 */
     DEFPAR( 8, "z-scale" , rval , sval , 1.0 , 0.0 , 0.0 ) ;

     /* shears = the final 3:
        The code below (for shear params) was modified 16 Jul 2014, to
        correct the labels (per user Mingbo) for the various EPI/FPS cases;
        see the usage of the 'a', 'b', 'c' parameters in defining the shear
        matrix 'ss' in function GA_setup_affine() in file mri_genalign.c.  */

     { char *alab , *blab , *clab ;
       switch( smat ){
         default:       alab = "y/x-shear" ; blab = "z/x-shear" ; clab = "z/y-shear" ; break ;
         case SMAT_XXX: alab = "y/x-shear" ; blab = "z/x-shear" ; clab = "unused"    ; break ;
         case SMAT_YYY: alab = "y/x-shear" ; blab = "z/y-shear" ; clab = "unused"    ; break ;
         case SMAT_ZZZ: alab = "z/x-shear" ; blab = "z/y-shear" ; clab = "unused"    ; break ;
       }
       rval = (do_small) ? 0.0555f : 0.1111f ;
       DEFPAR(  9, alab , -rval , rval , 0.0 , 0.0 , 0.0 ) ;
       DEFPAR( 10, blab , -rval , rval , 0.0 , 0.0 , 0.0 ) ;
       DEFPAR( 11, clab , -rval , rval , 0.0 , 0.0 , 0.0 ) ;
     }
   }

   /*-- adjustments for 2D images --*/

   if( twodim_code > 0 ){               /* 03 Dec 2010 */
     int i1=0,i2=0,i3=0,i4=0,i5=0,i6=0 ;
     switch( twodim_code ){             /* 2D images: freeze some parameters */
       case 3:                               /* axial slice == k-axis is I-S */
         i1=3 ; i2=5 ; i3=6 ; i4=9 ; i5=11 ; i6=12 ; break ;
       case 2:                             /* coronal slice == k-axis is A-P */
         i1=2 ; i2=4 ; i3=5 ; i4=8 ; i5=10 ; i6=12 ; break ;
       case 1:                            /* sagittal slice == k-axis is L-R */
         i1=1 ; i2=4 ; i3=6 ; i4=7 ; i5=10 ; i6=11 ; break ;
     }
     if( i1 > 0 ){
       stup.wfunc_param[i1-1].fixed = 2 ; /* fixed==2 means cannot be unfixed */
       stup.wfunc_param[i2-1].fixed = 2 ; /* fixed==1 is 'temporarily fixed'  */
       stup.wfunc_param[i3-1].fixed = 2 ;
       stup.wfunc_param[i4-1].fixed = 2 ;
       stup.wfunc_param[i5-1].fixed = 2 ;
       stup.wfunc_param[i6-1].fixed = 2 ;
       if( verb && apply_mode == 0 )
         INFO_message("base dataset is 2D ==> froze out-of-plane affine parameters") ;
     }
   }

   /*-- apply any parameter-altering user commands --*/

   for( ii=0 ; ii < nparopt ; ii++ ){
     jj = paropt[ii].np ;
     if( jj < stup.wfunc_numpar ){
       if( stup.wfunc_param[jj].fixed && verb )
         ININFO_message("Altering fixed param#%d [%s]" ,
                        jj+1 , stup.wfunc_param[jj].name ) ;

       switch( paropt[ii].code ){
         case PARC_FIX: stup.wfunc_param[jj].fixed     = 2 ; /* permanent fix */
                        stup.wfunc_param[jj].val_fixed = paropt[ii].vb;
         if( verb > 1 )
           ININFO_message("Fix param#%d [%s] = %f",
                          jj+1 , stup.wfunc_param[jj].name ,
                                 stup.wfunc_param[jj].val_fixed ) ;
         break;

         case PARC_INI: stup.wfunc_param[jj].fixed     = 0 ;
                        stup.wfunc_param[jj].val_fixed =
                        stup.wfunc_param[jj].val_init  =
                        stup.wfunc_param[jj].val_pinit = paropt[ii].vb;
         if( verb > 1 )
           ININFO_message("Init param#%d [%s] = %f",
                          jj+1 , stup.wfunc_param[jj].name ,
                                 stup.wfunc_param[jj].val_pinit ) ;
         break;

         case PARC_RAN:{
           float vb = paropt[ii].vb , vt = paropt[ii].vt ;
           if( do_cmass ){  /* 06 Aug 2007 */
             switch(jj){
               case 0: vb += xc ; vt += xc ; break ;
               case 1: vb += yc ; vt += yc ; break ;
               case 2: vb += zc ; vt += zc ; break ;
             }
           }
           /** stup.wfunc_param[jj].fixed = 0 ; **/
           stup.wfunc_param[jj].min   = vb;
           stup.wfunc_param[jj].max   = vt;
           if( verb > 1 )
             ININFO_message("Range param#%d [%s] = %f .. %f  center = %f",
                            jj+1 , stup.wfunc_param[jj].name ,
                                   stup.wfunc_param[jj].min  ,
                                   stup.wfunc_param[jj].max  ,
                    0.5f*(stup.wfunc_param[jj].min+stup.wfunc_param[jj].max) );
         }
         break;
       }
     } else {
       WARNING_message("Can't alter parameter #%d: out of range :-(",jj+1) ;
     }
   }

   /*-- check to see if we have free parameters so we can actually do something --*/

   for( ii=jj=0 ; jj < stup.wfunc_numpar ; jj++ )  /* count free params */
     if( !stup.wfunc_param[jj].fixed ) ii++ ;
   if( ii == 0 ) ERROR_exit("No free parameters for aligning datasets?! :-(") ;
   nparam_free = ii ;
   if( verb > 1 && apply_mode == 0 ) ININFO_message("%d free parameters",ii) ;

   /*-- should have some free parameters in the first 6 if using twopass --*/

   if( twopass ){
     for( ii=jj=0 ; jj < stup.wfunc_numpar && jj < 6 ; jj++ )
       if( !stup.wfunc_param[jj].fixed ) ii++ ;
     if( ii == 0 ){
       WARNING_message("Disabling twopass because no free parameters in first 6!?");
       twopass = 0 ;
     }
   }

   /*-- set normalized convergence radius for parameter search --*/

   /* get size of box we are dealing with */
   if( im_weig == NULL ){
     xsize = xxx = 0.5f * (nx_base-1) * dx_base ;
     ysize = yyy = 0.5f * (ny_base-1) * dy_base ;
     zsize = zzz = 0.5f * (nz_base-1) * dz_base ;
   } else {
     int xm,xp , ym,yp , zm,zp ;
     MRI_autobbox_clust(0) ;
     MRI_autobbox( im_weig , &xm,&xp , &ym,&yp , &zm,&zp ) ;
     MRI_autobbox_clust(1) ;
#if 0
     fprintf(stderr,"xm,xp,nx=%d,%d,%d\n",xm,xp,nx_base) ;
     fprintf(stderr,"ym,yp,ny=%d,%d,%d\n",ym,yp,ny_base) ;
     fprintf(stderr,"zm,zp,nz=%d,%d,%d\n",zm,zp,nz_base) ;
#endif
     xsize = xxx = 0.5f * (xp-xm) * dx_base ;
     ysize = yyy = 0.5f * (yp-ym) * dy_base ;
     zsize = zzz = 0.5f * (zp-zm) * dz_base ;
   }
   /* convert box size to a single size */
   xxx = (nz_base > 1) ? cbrt(xxx*yyy*zzz) : sqrt(xxx*yyy) ;
   zzz = 0.01f ;  /* smallest normalized value */
   for( jj=0 ; jj < 9 && jj < stup.wfunc_numpar ; jj++ ){ /* loop over params */
     if( stup.wfunc_param[jj].fixed ) continue ;             /* except shears */
     siz = stup.wfunc_param[jj].max - stup.wfunc_param[jj].min ; /* par range */
     if( siz <= 0.0f ) continue ;
        /* normalization = conv_mm scaled by mm size of the param range since */
        /*    optimization is done on unitless params scaled to [-1..1] range */
          if( jj < 3 ) yyy = conv_mm / siz ;                   /* shift param */
     else if( jj < 6 ) yyy = 57.3f * conv_mm / (xxx*siz) ;     /* angle param */
     else              yyy = conv_mm / (xxx*siz) ;             /* scale param */
     zzz = MIN(zzz,yyy) ;               /* smallest scaled value found so far */
   }
   conv_rad = MIN(zzz,0.001f); conv_rad = MAX(conv_rad,0.000005f);  /* limits */
   if( verb > 1 && apply_mode == 0 )
     INFO_message("Normalized (unitless) convergence radius = %.7f",conv_rad) ;

   /*-- print parameter ranges [10 Mar 2020] --*/

   if( verb > 1 ){
     INFO_message("Final parameter search ranges:") ;
     for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
       if( !stup.wfunc_param[jj].fixed )
         ININFO_message(" %12s = %8.3f .. %8.3f",
                        stup.wfunc_param[jj].name ,
                        stup.wfunc_param[jj].min  ,
                        stup.wfunc_param[jj].max   ) ;
     }
   }

   /*-- special case: 04 Apr 2008 --*/

   switch( apply_mode ){  /* all this is for -nwarp inputs (obsolescent) */
     default:                                  break ;
     case APPLY_BILIN: SETUP_BILINEAR_PARAMS ; break ;
     case APPLY_CUBIC: SETUP_CUBIC_PARAMS    ; break ;
     case APPLY_QUINT: SETUP_QUINT_PARAMS    ; break ;
     case APPLY_HEPT : SETUP_HEPT_PARAMS     ; break ;
     case APPLY_NONI : SETUP_NONI_PARAMS     ; break ;
   }

   /*****------ create shell of output dataset ------*****/

   if( prefix == NULL ){
     WARNING_message("No output dataset will be calculated") ;
     if( dxyz_mast > 0.0 )
       WARNING_message("-mast_dxyz %g option was meaningless :-(",dxyz_mast) ;
   } else {
     if( dset_mast == NULL ){ /* pick a master dataset to control output grid */
       if( dset_base != NULL ){
         if( verb ) INFO_message("master dataset for output = base") ;
         dset_mast = dset_base ;
       } else {
         if( verb ) INFO_message("master dataset for output = source") ;
         dset_mast = dset_targ ;
       }
     }
     if( dxyz_mast > 0.0 ){   /* 24 Jul 2007 -- alter grid size */
       THD_3dim_dataset *qset ;
       qset = r_new_resam_dset( dset_mast , NULL ,
                                dxyz_mast,dxyz_mast,dxyz_mast ,
                                NULL , RESAM_NN_TYPE , NULL , 0 , 0) ;
       if( qset != NULL ){
         dset_mast = qset ;
         THD_daxes_to_mat44(dset_mast->daxes) ;
         if( verb )
           INFO_message("changing output grid spacing to %.4f mm",dxyz_mast) ;
       }
     }
     if( !ISVALID_MAT44(dset_mast->daxes->ijk_to_dicom) ) /* make sure have */
       THD_daxes_to_mat44(dset_mast->daxes) ;      /* index-to-DICOM matrix */
     mast_cmat     = DSET_CMAT(dset_mast,use_realaxes) ;
     mast_cmat_inv = MAT44_INV(mast_cmat) ;

     dset_out = EDIT_empty_copy( dset_mast ) ;  /* create the output dataset! */
     EDIT_dset_items( dset_out ,                /* and patch it up */
                        ADN_prefix    , prefix ,
                        ADN_nvals     , DSET_NVALS(dset_targ) ,
                        ADN_datum_all , MRI_float ,
                      ADN_none ) ;
     /* do not let time info from master confuse things, we'll go back */
     /* to ntt > 1 later, if approprate             [1 Jun 2020 rickr] */
     if( DSET_NUM_TIMES(dset_out) > 1 )
         EDIT_dset_items( dset_out ,   ADN_ntt , 1 , ADN_none ) ;

       if( DSET_NUM_TIMES(dset_targ) > 1 )
         EDIT_dset_items( dset_out ,
                            ADN_ntt   , DSET_NVALS(dset_targ) ,
                            ADN_ttdel , DSET_TR(dset_targ) ,
                            ADN_tunits, UNITS_SEC_TYPE ,
                            ADN_nsl   , 0 ,
                          ADN_none ) ;
       else
         EDIT_dset_items( dset_out ,
                            ADN_func_type , ISANAT(dset_out) ? ANAT_BUCK_TYPE
                                                             : FUNC_BUCK_TYPE ,
                          ADN_none ) ;

     /* copy brick info into output */

     THD_copy_datablock_auxdata( dset_targ->dblk , dset_out->dblk ) ;
     if (!THD_copy_labeltable_atr( dset_out->dblk,  dset_targ->dblk)) {
      WARNING_message("Failed trying to preserve labeltables");
     }
     for( kk=0 ; kk < DSET_NVALS(dset_out) ; kk++ )
       EDIT_BRICK_FACTOR(dset_out,kk,0.0);

     tross_Copy_History( dset_targ , dset_out ) ;        /* historic records */
     tross_Make_History( "3dAllineate" , argc,argv , dset_out ) ;

     memset(&cmat_tout,0,sizeof(mat44)) ;
     memset(&cmat_bout,0,sizeof(mat44)) ;
     THD_daxes_to_mat44(dset_out->daxes) ;          /* save coord transforms */
     cmat_tout = DSET_CMAT(dset_targ,use_realaxes) ;
     cmat_bout = DSET_CMAT(dset_out ,use_realaxes) ;
     nxout = DSET_NX(dset_out) ; dxout = fabsf(DSET_DX(dset_out)) ;
     nyout = DSET_NY(dset_out) ; dyout = fabsf(DSET_DY(dset_out)) ;
     nzout = DSET_NZ(dset_out) ; dzout = fabsf(DSET_DZ(dset_out)) ;
     nxyz_dout[0] = nxout; nxyz_dout[1] = nyout; nxyz_dout[2] = nzout;
     dxyz_dout[0] = dxout; dxyz_dout[1] = dyout; dxyz_dout[2] = dzout;
   }

   /* check if have dataset prefix for saving the 3D warp */

   if( dset_out == NULL && nwarp_save_prefix != NULL ){
     WARNING_message("Can't use -nwarp_save without -prefix! :-(") ;
     nwarp_save_prefix = NULL ;
   }

   /***~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~***/
   /***---------------------- start alignment process ----------------------***/

#ifdef USE_OMP
#pragma omp parallel
 {
  if( verb && omp_get_thread_num() == 0 )
    INFO_message("OpenMP thread count = %d",omp_get_num_threads()) ;
 }
#endif

/* macros useful for verbosity */

#undef  PARDUMP  /* xxx = field name in param */
#define PARDUMP(ss,xxx)                                     \
  do{ fprintf(stderr," + %s Parameters =",ss) ;             \
      for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){          \
        if( jj == 12 ) fprintf(stderr," |") ;               \
        fprintf(stderr," %.4f",stup.wfunc_param[jj].xxx) ;  \
      }                                                     \
      fprintf(stderr,"\n") ;                                \
  } while(0)

#undef  PAROUT
#define PAROUT(ss) PARDUMP(ss,val_out)

#undef  PARINI
#define PARINI(ss) PARDUMP(ss,val_init)

#undef  PARVEC
#define PARVEC(ss,vv)                              \
  do{ fprintf(stderr," + %s Parameters =",ss) ;    \
      for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )  \
        fprintf(stderr," %.4f",vv[jj]) ;           \
      fprintf(stderr,"\n") ;                       \
  } while(0)

/* copy parameter set 'xxx' from stup to the
   allpar array, for ease of use in calling other functions */

#undef  PAR_CPY
#define PAR_CPY(xxx)                              \
  do{ for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ) \
        allpar[jj] = stup.wfunc_param[jj].xxx ;   \
  } while(0)

/* for the final output of parameters, with commentary */

#undef  PARLIST
#define PARLIST(ss,xxx)                                     \
  do{ fprintf(stderr," + %s Parameters:\n    ",ss) ;        \
      for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){          \
        if( jj > 0 && jj%3 == 0 ) fprintf(stderr,"\n    "); \
        fprintf(stderr," %9s=%8.4f",                        \
     stup.wfunc_param[jj].name,stup.wfunc_param[jj].xxx) ;  \
        if( jj == 2 )                                              \
         fprintf(stderr,"  ...  enorm=%8.4f mm",                   \
          sqrt( stup.wfunc_param[0].xxx*stup.wfunc_param[0].xxx    \
               +stup.wfunc_param[1].xxx*stup.wfunc_param[1].xxx    \
               +stup.wfunc_param[2].xxx*stup.wfunc_param[2].xxx)); \
        else if( jj == 5 )                                  \
         fprintf(stderr,"  ...  total=%8.4f deg",           \
          total_rotation_degrees(stup.wfunc_param[3].xxx,   \
                                 stup.wfunc_param[4].xxx,   \
                                 stup.wfunc_param[5].xxx)); \
        else if( jj == 8 ){                                 \
         float fff = stup.wfunc_param[6].xxx                \
                    *stup.wfunc_param[7].xxx                \
                    *stup.wfunc_param[8].xxx ;              \
         fprintf(stderr,"  ...  vol3D=%8.4f %s",            \
           fff ,                                            \
           (fff < 1.0f )                                    \
            ? "= base bigger than source"                   \
            : "= base smaller than source" ) ;              \
        }                                                   \
      }                                                     \
      fprintf(stderr,"\n") ;                                \
  } while(0)
#undef  PARNOUT
#define PARNOUT(ss) PARLIST(ss,val_out)   /* 30 Aug 2013 */

   /*-- the Annunciation --*/

   if( do_allcost >= 0 && verb ){
     if( apply_1D == NULL )
       INFO_message("======= Allineation of %d sub-bricks using %s =======",
                    DSET_NVALS(dset_targ) , meth_username[meth_code-1] ) ;
     else
       INFO_message("========== Applying transformation to %d sub-bricks ==========",
                    DSET_NVALS(dset_targ) ) ;
   }

   if( verb > 1 ) mri_genalign_verbose(verb-1) ;  /* inside mri_genalign.c */

   /*-- array in which to save parameters for later waterboarding --*/

   if( param_save_1D != NULL || apply_mode != APPLY_AFF12 )
     parsave = (float **)calloc(sizeof(float *),DSET_NVALS(dset_targ)) ;

   if( !NONLINEAR_APPLY(apply_mode) ){                                  /* 04 Apr 2008 */
    if( matrix_save_1D != NULL || apply_mode != APPLY_AFF12  )
      matsave = (mat44 * )calloc(sizeof(mat44),DSET_NVALS(dset_targ)) ; /* 23 Jul 2007 */
   }

/* as you might guess, this is for saving joint histogram to a file */
#undef  SAVEHIST
#define SAVEHIST(nnn,docc)                                                 \
 do{ int nbin ; float *xyc ;                                               \
     if( docc ) (void)mri_genalign_scalar_cost( &stup , NULL ) ;           \
     nbin = retrieve_2Dhist( &xyc ) ;                                      \
     if( nbin > 0 && xyc != NULL ){                                        \
       char fname[256] ; MRI_IMAGE *fim ; double ftop ;                    \
       fim = mri_new(nbin,nbin,MRI_float); mri_fix_data_pointer(xyc,fim);  \
       if( strstr(save_hist,"FF") == NULL ){                               \
         ftop = mri_max(fim) ; qim = mri_to_byte_scl(255.4/ftop,0.0,fim) ; \
         mri_clear_data_pointer(fim); mri_free(fim);                       \
         fim = mri_flippo(MRI_ROT_90,0,qim); mri_free(qim);                \
         sprintf(fname,"%s_%s_%04d.pgm",save_hist,nnn,kk) ;                \
         mri_write_pnm(fname,fim); mri_free(fim);                          \
       } else {                                                            \
         qim = mri_flippo(MRI_ROT_90,0,fim);                               \
         mri_clear_data_pointer(fim); mri_free(fim);                       \
         sprintf(fname,"%s_%s_%04d.mri",save_hist,nnn,kk) ;                \
         mri_write(fname,qim); mri_free(qim);                              \
       }                                                                   \
       if( verb ) ININFO_message("- Saved histogram to %s",fname) ;        \
     }                                                                     \
 } while(0)

   /***-------------------- loop over target sub-bricks --------------------***/

   im_bset = im_base ;  /* base image for first loop */
   im_wset = im_weig ;

   /** 3dUnifize the base image? [23 Dec 2016] **/

#ifdef ALLOW_UNIFIZE
   if( do_unifize_base && dset_base != NULL && nz_base > 5 && apply_1D == NULL ){
     THD_3dim_dataset *qset, *uset ;
     char *uuu, bname[32], uname[32] , cmd[1024] ;
     float *bar , urad ;

     uuu = UNIQ_idcode_11() ;
     sprintf(uname,"UU.%s.nii",uuu) ;
     sprintf(bname,"BB.%s.nii",uuu) ;

     qset = THD_image_to_dset(im_bset,bname) ;
     qset->dblk->diskptr->storage_mode = STORAGE_BY_NIFTI ;
     DSET_write(qset) ;

     urad = 18.3f / cbrtf(dx_base*dy_base*dz_base) ;
          if( urad < 5.01f ) urad = 5.01f ;
     else if( urad > 23.3f ) urad = 23.3f ;
     sprintf(cmd,
             "3dUnifize -input %s -prefix %s -T2 -Urad %.2f -clfrac 0.333",
             bname , uname , urad ) ;
     INFO_message("About to do -unifize_base:\n  %s",cmd) ;
     system(cmd) ;
     THD_delete_3dim_dataset(qset,True) ;

     uset = THD_open_dataset(uname) ;
     if( uset == NULL ){
       WARNING_message("-unifize_base failed :(") ;
     } else {
       DSET_load(uset) ;
       if( !DSET_LOADED(uset) ){
         WARNING_message("-unifize_base did something weird :((") ;
       } else {
         im_bset = im_ubase = mri_copy(DSET_BRICK(uset,0)) ;
       }
       THD_delete_3dim_dataset(uset,True) ;
     }
   } /* end of -unifize_base */
#endif  /* ALLOW_UNIFIZE */

   /* for filling the outside of the target mask with random crap */

   stup.ajmask_ranfill = 0 ;                          /* 02 Mar 2010: oops */
   if( im_tmask != NULL ){
     mri_genalign_set_targmask( im_tmask , &stup ) ;  /* 07 Aug 2007 */
     mri_free(im_tmask) ; im_tmask = NULL ;           /* is copied inside */
     if( fill_source_mask ) stup.ajmask_ranfill = 1 ; /* 01 Mar 2010 */
   }

   /* for the overlap portion of lpc+ and lpa+ */

   if( !APPLYING && micho_ov != 0.0 ){
     byte *mmm ; int ndil=auto_tdilation ; MRI_IMAGE *bsm ;
     mmm = mri_automask_image(im_base) ;
     if( mmm != NULL ){
       bsm = mri_new_vol_empty( nx_base,ny_base,nz_base , MRI_byte ) ;
       mri_fix_data_pointer( mmm , bsm ) ;
       if( ndil > 0 ){
         for( ii=0 ; ii < ndil ; ii++ ){
           THD_mask_dilate     ( nx_base,ny_base,nz_base , mmm , 3, 2 ) ;
           THD_mask_fillin_once( nx_base,ny_base,nz_base , mmm , 2 ) ;
         }
       }
       mri_genalign_set_basemask( bsm , &stup ) ;
       mri_free(bsm) ;
     }
   }

   MEMORY_CHECK("about to start alignment loop") ;

   if( sm_rad == 0.0f &&
       ( meth_code == GA_MATCH_PEARSON_LOCALS   ||
         meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
         meth_code == GA_MATCH_LPA_MICHO_SCALAR ||
         meth_code == GA_MATCH_PEARSON_LOCALA     ) ) sm_rad = MAX(2.222f,dxyz_top) ;

   /*------ process the target dataset volumes, one at a time ------*/

   for( kk=0 ; kk < DSET_NVALS(dset_targ) ; kk++ ){  /** the sub-brick loop **/

     stup.match_code = meth_code ;

     ZERO_MAT44(aff12_xyz) ; /* 23 Jul 2007: invalidate the matrix */

     bfac = DSET_BRICK_FACTOR(dset_targ,kk) ;  /* sub-brick scale factor [14 Oct 2008] */

     skipped = 0 ;
     if( kk == 0 && skip_first ){  /* skip first image since it == im_base */
       if( verb )
         INFO_message("========= Skipping sub-brick #0: it's also base image =========");
       DSET_unload_one(dset_targ,0) ;

       /* load parameters with identity transform */

       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )   /* for -1Dfile output */
         stup.wfunc_param[jj].val_out = stup.wfunc_param[jj].ident ;

       /* load aff12_xyz matrix with identity transform [23 Jul 2007] */

       LOAD_DIAG_MAT44(aff12_xyz,1.0f,1.0f,1.0f) ;
       skipped = 1 ; goto WRAP_IT_UP_BABY ;
     }

     /*-- make copy of target brick, and process that --*/

     /* show if verbose, else use light output     7 Jul 2017 [rickr] */
     if( verb > 1 )
       INFO_message("========== sub-brick #%d ========== [total CPU to here=%.1f s]",
                    kk , COX_cpu_time() ) ;
     else if ( verb && DSET_NVALS(dset_targ) > 1 ) {
       if( kk == 0 ) fprintf(stderr,"volume 0");
       else          fprintf(stderr,"..%d", kk);
       if( kk == DSET_NVALS(dset_targ)-1 ) fputc('\n', stderr);
     }

     /* extract data to be aligned */

     im_targ = mri_scale_to_float( bfac , DSET_BRICK(dset_targ,kk) ) ;
     if( im_targ == NULL )
       ERROR_exit("Cannot extract float image from source dataset :(") ;

     if( targ_was_vector ){  /* 12 May 2020 (for RGB images) */
       if( im_targ_vector != NULL ) mri_free(im_targ_vector) ;
       im_targ_vector = mri_copy( DSET_BRICK(dset_targ,kk) ) ;  /* for use at end */
     }
     DSET_unload_one(dset_targ,kk) ; /* it's been copied in, so can unload it now */

     /* clip off negative values from the target? */

     if( do_zclip ){
       float *bar = MRI_FLOAT_PTR(im_targ) ;
       for( ii=0 ; ii < im_targ->nvox ; ii++ ) if( bar[ii] < 0.0f ) bar[ii] = 0.0f ;
     }

     /* check for empty-ish volume */

     nnz = mri_nonzero_count(im_targ) ;
     if( nnz < 66 )
       WARNING_message("3dAllineate :: source image #%d has %d nonzero voxel%s (< 66)",
                       kk , nnz , (nnz==1) ? "\0" : "s" ) ;

     /*** if we are just applying input parameters, set up for that now ***/
     /*** we set output params as if they had been found by optimizing  ***/

     if( apply_1D != NULL ){
       int rr=kk ;
       if( rr >= apply_ny ){  /* 19 Jul 2007 */
         rr = apply_ny-1 ;
         WARNING_message("Re-using final row of -1D*_apply '%s' for sub-brick #%d",
                         apply_1D , kk ) ;
       }
       stup.interp_code = final_interp ;
       stup.smooth_code = 0 ;
       stup.npt_match   = 11 ;
       mri_genalign_scalar_setup( im_bset , NULL , im_targ , &stup ) ;
       im_bset = NULL ;  /* after setting base, don't need to set it again */
       mri_free(im_targ) ; im_targ = NULL ;

       switch( apply_mode ){   /* 23 Jul 2007 */
         default:
         case APPLY_BILIN:
         case APPLY_PARAM:     /* load parameters from file into structure */
           if( verb > 1 ) INFO_message("using -1Dparam_apply") ;
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
             stup.wfunc_param[jj].val_out = APL(jj,rr) ;
         break ;

         case APPLY_AFF12:     /* load matrix from file into aff12_xyz */
           if( verb > 1 ) INFO_message("using -1Dmatrix_apply") ;
           LOAD_MAT44_AR( aff12_xyz , &APL(0,rr) ) ;    /* DICOM coord matrix */
         break ;
       }
       goto WRAP_IT_UP_BABY ;
     }

     /*--- at this point, am actually going to do optimization ---*/

     /* initialize parameters (for the -onepass case) */

     for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
       stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_pinit ;

     /* initialize coordinate systems */

     AL_setup_warp_coords( epi_targ,epi_fe,epi_pe,epi_se,
                           nxyz_base, dxyz_base, stup.base_cmat,
                           nxyz_targ, dxyz_targ, stup.targ_cmat ) ;

     if( do_allcost != 0 ){  /*-- print all cost functionals, for fun? --*/

       stup.interp_code = MRI_LINEAR ;
       stup.npt_match   = npt_match ;
       if( do_allcost < 0 && fine_rad > 0.0f ){
         stup.smooth_code        = sm_code ;
         stup.smooth_radius_base = stup.smooth_radius_targ = fine_rad ;
       }

       mri_genalign_scalar_setup( im_bset , im_wset , im_targ , &stup ) ;

       if( allcostX1D == NULL ){ /* just do init parameters == the old way */

         PAR_CPY(val_init) ;   /* copy init parameters into the allpar arrary */
         allcost = mri_genalign_scalar_allcosts( &stup , allpar ) ;
         PARINI("initial") ;
         INFO_message("allcost output: init #%d",kk) ;
         for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
           fprintf(stderr,"   %-4s = %g\n",meth_shortname[jj],allcost->ar[jj]) ;
         KILL_floatvec(allcost) ;

         if( do_allcost == -1 ){
           SAVE_PEARSON_MAP(save_pearson_prefix,val_init) ;
           do_save_pearson_map = 0 ;
         }

         if( save_hist != NULL ) SAVEHIST("allcost_init",0) ;
         if( do_allcost == -1 ) continue ;  /* skip to next sub-brick */

       } else {  /* 02 Sep 2008: do a bunch of parameter vectors */

         float *av=MRI_FLOAT_PTR(allcostX1D); int nxp=allcostX1D->nx; FILE *fp;

         if( strcmp(allcostX1D_outname,"-")      == 0 ||
             strcmp(allcostX1D_outname,"stdout") == 0   ){
           fp = stdout ;
         } else {
           fp = fopen( allcostX1D_outname , "w" ) ;
           if( fp == NULL )
             ERROR_exit("Can't open file '%s' for -allcostX1D output :-(" ,
                        allcostX1D_outname ) ;
         }
         INFO_message("Writing -allcostX1D results to '%s'",allcostX1D_outname) ;
         fprintf( fp , "# 3dAllineate -allcostX1D results:\n" ) ;
         fprintf( fp , "#" ) ;
         for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
           fprintf( fp , "  ___ %-4s ___",meth_shortname[jj]) ;
         fprintf( fp , "\n") ;
         for( ii=0 ; ii < allcostX1D->ny ; ii++ ){
           allcost = mri_genalign_scalar_allcosts( &stup , av + ii*nxp ) ;
           fprintf( fp , " " ) ;
           for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
             fprintf( fp , " %12.6f" , allcost->ar[jj] ) ;
           fprintf( fp , "\n") ;
           KILL_floatvec(allcost) ;
           if( save_hist != NULL ){
             char fn[32] ; sprintf(fn,"allcost%06d",ii) ; SAVEHIST(fn,0) ;
           }
         }
         if( fp != stdout ) fclose(fp) ;
         INFO_message("-allcostX1D finished") ; exit(0) ;
       }
     }

     /*-------- do coarse resolution pass? --------*/

     didtwo = 0 ;
     if( twopass && (!twofirst || !tfdone) ){

       int tb , ib , ccode , nrand ; char *eee ;

       if( verb ) INFO_message("*** Coarse pass begins ***") ;
       /* used to do NN in the coarse pass, but found than Linear worked better */
       ccode            = (interp_code == MRI_NN) ? MRI_NN : MRI_LINEAR ;
       stup.interp_code = ccode ;
       stup.npt_match   = ntask / 15 ;  /* small number of matching points */
       if( stup.npt_match < nmatch_setup ) stup.npt_match = nmatch_setup;

       stup.smooth_code        = sm_code ;
       stup.smooth_radius_base =
        stup.smooth_radius_targ = (sm_rad == 0.0f) ? 7.777f : sm_rad ;

       mri_genalign_scalar_setup( im_bset , im_wset , im_targ , &stup ) ;
       im_bset = NULL; im_wset = NULL;  /* after being set, needn't set again */
       if( usetemp ){
         mri_purge(im_targ); mri_purge(im_base); mri_purge(im_weig);
       }

       if( save_hist != NULL ) SAVEHIST("start",1) ;

       /*- search for coarse start parameters, then optimize them? -*/

       if( tbest > 0 ){  /* default tbest==5 */
         int nrefine ;

         if( verb > 1 ) ININFO_message("- Search for coarse starting parameters") ;

         /* startup search only allows up to 6 parameters, so freeze excess */

         eee = my_getenv("AFNI_TWOPASS_NUM") ;  /* normally, eee is NULL */
         if( eee == NULL || *eee != ':' ){      /* so this branch is taken */
           if( eee != NULL ) sscanf( eee , "%d" , &nptwo ) ;
           if( nptwo < 1 || nptwo > 6 ) nptwo = 6 ;
           if( nparam_free > nptwo ){  /* old way: just free first nptwo params */
             for( ii=jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
               if( !stup.wfunc_param[jj].fixed ){
                 ii++ ;  /* number free so far */
                 if( ii > nptwo ) stup.wfunc_param[jj].fixed = 1 ;  /* temp freeze */
               }
             }
           }
         } else {                      /* the new way: free from a list */
           int npk[6]={-1,-1,-1,-1,-1,-1} ;
           sscanf( eee , ":%d:%d:%d:%d:%d:%d" ,
                   npk+0 , npk+1 , npk+2 , npk+3 , npk+4 , npk+5 ) ;
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
             if( !stup.wfunc_param[jj].fixed ) stup.wfunc_param[jj].fixed = 1 ;
           }
           for( ii=0 ; ii < 6 ; ii++ ){
             jj = npk[ii] ;
             if( jj >= 0 && jj < stup.wfunc_numpar && stup.wfunc_param[jj].fixed == 1 )
               stup.wfunc_param[jj].fixed = 0 ;
           }
         }

         /* do the startup parameter search:
              saves best param set in val_init (and val_out),
              plus a few more good sets in val_trial for refinement */

         if( verb > 1 ) ctim = COX_cpu_time() ;

         powell_set_mfac( 1.0f , 3.0f ) ;  /* 07 Jun 2011 - for some speedup */

         nrand = 17 + 4*tbest ; nrand = MAX(nrand,31) ; /* num random param setups to try */

         mri_genalign_scalar_ransetup( &stup , nrand ) ;  /**** the initial search! ****/

         if( verb > 1 )
           ININFO_message("- Coarse startup search net CPU time = %.1f s",COX_cpu_time()-ctim);

         /* unfreeze those parameters that were temporarily frozen above */

         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           if( stup.wfunc_param[jj].fixed == 1 ) stup.wfunc_param[jj].fixed = 0 ;

         /*-- now refine the tbest values saved already (from val_trial) --*/

         tb = MIN(tbest,stup.wfunc_ntrial) ; nfunc=0 ;
         if( verb > 1 ) ctim = COX_cpu_time() ;

         /* copy parameters out */

         for( ib=0 ; ib < tb ; ib++ )
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
             tfparm[ib][jj] = stup.wfunc_param[jj].val_trial[ib] ;

         /* add identity transform to set, for comparisons (and insurance) */

         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           tfparm[tb][jj] = stup.wfunc_param[jj].val_pinit ;

         tfdone = tb+1 ;  /* number of parameter sets now saved in tfparm */

         nrefine = (int)AFNI_numenv("AFNI_TWOPASS_REFINE") ;
         if( nrefine <= 0 || nrefine >= 3 ) nrefine = 3 ;  /* number of refinement passes */
         rad = 0.0444 ;  /* initial search radius in parameter space */

         /* loop over refinement passes: try to make each trial parameter set better */

         for( rr=0 ; rr < nrefine ; rr++ , rad*=0.6789 ){ /* refine with less smoothing */

           if( verb > 1 )
             INFO_message("Start refinement #%d on %d coarse parameter sets",rr+1,tfdone);

           powell_set_mfac( 1.0f , 5.0f+2.0f*rr ) ;  /* 07 Jun 2011 */

           stup.smooth_radius_base *= 0.7777 ;  /* less smoothing */
           stup.smooth_radius_targ *= 0.7777 ;
           stup.smooth_radius_base = MAX(stup.smooth_radius_base,fine_rad) ;
           stup.smooth_radius_targ = MAX(stup.smooth_radius_targ,fine_rad) ;

           stup.npt_match          *= 1.5 ;     /* more voxels for matching */
           mri_genalign_scalar_setup( NULL,NULL,NULL , &stup ) ;

           for( ib=0 ; ib < tfdone ; ib++ ){              /* loop over param sets */
             for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )  /* load parameters */
               stup.wfunc_param[jj].val_init = tfparm[ib][jj] ;

             /* optimize a little */

             nfunc += mri_genalign_scalar_optim( &stup, rad, 0.0666*rad, 99 ) ;

             for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )  /* save optimized params */
               tfparm[ib][jj] = stup.wfunc_param[jj].val_out ;

             tfcost[ib] = stup.vbest ; tfindx[ib] = ib ;  /* save cost */
             if( verb > 1 )
               ININFO_message("- param set #%d has cost=%f",ib+1,stup.vbest) ;
             if( verb > 2 ) PAROUT("--") ;
           }

           /* 29 Aug 2008: sort tfparm by cost, then cast out the close ones */

           if( !nocast && tfdone > 2 ){
             int jb,ncast=0 ; float pdist ;

             if( verb > 1 ) ININFO_message("- sorting parameter sets by cost") ;
             for( ib=0 ; ib < tfdone ; ib++ )       /* copy tfparm into ffparm */
               memcpy( ffparm[ib], tfparm[ib], sizeof(float)*stup.wfunc_numpar );
             qsort_floatint( tfdone , tfcost , tfindx ) ;      /* sort by cost */
             for( ib=0 ; ib < tfdone ; ib++ ){        /* copy back into tfparm */
               jb = tfindx[ib] ;      /* jb = index in unsorted copy in ffparm */
               memcpy( tfparm[ib], ffparm[jb], sizeof(float)*stup.wfunc_numpar );
             }

             /* now cast out parameter sets that are very close to the best one */

#undef  CTHRESH
#define CTHRESH 0.02f
             if( verb > 1 ) ININFO_message("- scanning for distances from #1") ;
             for( ib=1 ; ib < tfdone ; ib++ ){
               pdist = param_dist( &stup , tfparm[0] , tfparm[ib] ) ;
               if( verb > 2 ) ININFO_message("--- dist(#%d,#1) = %.3g %s" ,
                                             ib+1, pdist, (pdist<CTHRESH)?"XXX":"" ) ;
               if( tfdone > 2 && pdist < CTHRESH ){
                 for( jb=ib+1 ; jb < tfdone ; jb++ )  /* copy those above down */
                   memcpy( tfparm[jb-1], tfparm[jb], sizeof(float)*stup.wfunc_numpar );
                 ncast++ ; tfdone-- ;
               }
             }
             if( ncast > 0 && verb > 1 )
               ININFO_message(
                 "- cast out %d parameter set%s for being too close to best set" ,
                 ncast , (ncast==1)?"":"s" ) ;
           }

         } /* end of refinement loop (rr) */

         if( verb > 1 )
           ININFO_message("- Total coarse refinement net CPU time = %.1f s; %d funcs",
                          COX_cpu_time()-ctim,nfunc ) ;

         /* end of '-twobest x' for x > 0 */

       } else {  /*- if stoopid user did '-twobest 0' -*/
                 /*- just optimize coarse setup from default parameters -*/
                 /*- mimicking the 3 loop passes above --*/

         if( verb     ) ININFO_message("- Start coarse optimization with -twobest 0") ;
         if( verb > 1 ) ctim = COX_cpu_time() ;
         powell_set_mfac( 2.0f , 1.0f ) ;  /* 07 Jun 2011 */
         /* optimize pass 1 */
         nfunc = mri_genalign_scalar_optim( &stup , 0.05 , 0.005 , 444 ) ;
         if( verb > 2 ) PAROUT("--(a)") ;
         /* optimize pass 2 */
         stup.npt_match = ntask / 7 ;
         if( stup.npt_match < nmatch_setup  ) stup.npt_match = nmatch_setup ;
         stup.smooth_radius_base *= 0.456 ;
         stup.smooth_radius_targ *= 0.456 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL , &stup ) ;
         nfunc += mri_genalign_scalar_optim( &stup , 0.0333 , 0.00333 , 444 ) ;
         if( verb > 2 ) PAROUT("--(b)") ;
         /* optimize pass 2 */
         stup.smooth_radius_base *= 0.456 ;
         stup.smooth_radius_targ *= 0.456 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL , &stup ) ;
         nfunc += mri_genalign_scalar_optim( &stup , 0.0166 , 0.00166 , 444 ) ;
         if( verb > 2 ) PAROUT("--(c)") ;
         if( verb > 1 ) ININFO_message("- Coarse net CPU time = %.1f s; %d funcs",
                                       COX_cpu_time()-ctim,nfunc) ;
         if( verb     ) ININFO_message("- Coarse optimization:  best cost=%f",
                                       stup.vbest) ;

         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )  /* save best params */
           tfparm[0][jj] = stup.wfunc_param[jj].val_out ;
         tfdone = 1 ;  /* number of parameter sets saved in tfparm */

       } /* end of '-twobest 0' */

       /*- 22 Sep 2006: add default init params to the tfparm list -*/
       /*-              (so there will be at least 2 sets there)   -*/

       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         tfparm[tfdone][jj] = stup.wfunc_param[jj].val_pinit ;
       tfdone++ ;

       didtwo = 1 ;   /* mark that we did the first pass */

     } /*------------- end of twopass-ization -------------*/

     /*-----------------------------------------------------------------------*/
     /*----------------------- do final resolution pass ----------------------*/
     /*-------- which has less smoothing and more voxels for matching --------*/

     if( verb > 1 || (verb && kk == 0) ) /* 7 Jan 2019 [rickr] */
        INFO_message("*** Fine pass begins ***") ;
     ctim = COX_cpu_time() ;

     stup.interp_code = interp_code ;  /* set interpolation   */
     stup.smooth_code = sm_code ;      /* and smoothing codes */

     /*-- setup smoothing --*/

     if( fine_rad > 0.0f ){  /* if ordered by user */
       stup.smooth_radius_base = stup.smooth_radius_targ = fine_rad ;
     } else if( diffblur ){  /* if base finer resolution than target */
       float br,tr ;
       if( nz_base > 1 ){
         br = cbrt(dx_base*dy_base*dz_base) ;  /* base voxel size */
         tr = cbrt(dx_targ*dy_targ*dz_targ) ;  /* targ voxel size */
       } else {
         br = sqrt(dx_base*dy_base) ;
         tr = sqrt(dx_targ*dy_targ) ;
       }
       stup.smooth_radius_targ = 0.0f ;
       stup.smooth_radius_base = (tr <= 1.1f*br) ? 0.0f
                                                 : sqrt(tr*tr-br*br) ;
     }

     /*-- setup the optimization --*/

     stup.npt_match = npt_match ;
     if( didtwo )                                  /* did first pass already: */
       mri_genalign_scalar_setup( NULL,NULL,NULL, &stup ); /* simple re-setup */
     else {
       mri_genalign_scalar_setup( im_bset , im_wset , im_targ , &stup ) ;
       im_bset = NULL; im_wset = NULL;  /* after being set, needn't set again */
       if( usetemp ) mri_purge( im_targ ) ;
     }

     switch( tfdone ){                  /* initial param radius for optimizer */
        case 0: rad = 0.0345 ; break ;  /* this is size of initial trust region */
        case 1:                         /* -- in the unitless [-1..1] space */
        case 2: rad = 0.0266 ; break ;
       default: rad = 0.0166 ; break ;
     }
     if( rad < 22.2f*conv_rad ) rad = 22.2f*conv_rad ;

     /*-- choose initial parameters, based on interp_code cost functional --*/

     powell_set_mfac( powell_mm , powell_aa ) ;  /* 07 Jun 2011 */

     if( tfdone ){                           /* find best in tfparm array */
       int kb=0 , ib ; float cbest=1.e+33 ;


       if( verb > 1 )
         INFO_message("Picking best parameter set out of %d cases",tfdone) ;
       for( ib=0 ; ib < tfdone ; ib++ ){
         cost = mri_genalign_scalar_cost( &stup , tfparm[ib] ) ;
         if( verb > 1 ) ININFO_message("- cost(#%d)=%f %c",
                                       ib+1,cost,(cost<cbest)?'*':' ');
         if( verb > 2 ) PARVEC("--",tfparm[ib]) ;
         if( cost < cbest ){ cbest=cost ; kb=ib ; }  /* save best case */
       }

       if( num_rtb == 0 ){  /* 27 Aug 2008: this was the old way,  */
                            /* to just pick the best at this point */
         if( verb > 1 )
           ININFO_message("-num_rtb 0 ==> pick best of the %d cases (#%d)",tfdone,kb+1);
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )        /* copy best set */
           stup.wfunc_param[jj].val_init = tfparm[kb][jj] ; /* for fine work */

         for( ib=0 ; ib < tfdone ; ib++ )  /* save all cases into ffparm */
           memcpy( ffparm[ib], tfparm[ib], sizeof(float)*stup.wfunc_numpar ) ;

       } else {            /* now: try to make these a little better instead */
                           /*      and THEN choose the best one at that point */
         if( verb > 1 )
           ININFO_message("-num_rtb %d ==> refine all %d cases",num_rtb,tfdone);
         cbest = 1.e+33 ;
         for( ib=0 ; ib < tfdone ; ib++ ){
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
             stup.wfunc_param[jj].val_init = tfparm[ib][jj] ;
           nfunc = mri_genalign_scalar_optim( &stup, rad, 0.0777*rad,
                                              (ib==tfdone-1) ? 2*num_rtb : num_rtb );
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )       /* save refined */
             ffparm[ib][jj] = stup.wfunc_param[jj].val_out ; /* parameters */
           cost = stup.vbest ;
           if( verb > 1 ) ININFO_message("- cost(#%d)=%f %c",
                                         ib+1,cost,(cost<cbest)?'*':' ' );
           if( verb > 2 ) PAROUT("--") ;
           if( cost < cbest ){ cbest=cost ; kb=ib ; }  /* save best case */
         }
         if( verb > 1 ) ININFO_message("- case #%d is now the best",kb+1) ;
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           stup.wfunc_param[jj].val_init = ffparm[kb][jj] ;
       }
       cost_ini = cbest ;

     } else {  /*-- did not do first pass, so we start at default params --*/

       cost_ini = mri_genalign_scalar_cost( &stup , NULL ) ;

     }

     if( do_allcost != 0 ){  /*-- print out all cost functionals, for fun --*/
       PAR_CPY(val_init) ;   /* copy init parameters into allpar[] */
       allcost = mri_genalign_scalar_allcosts( &stup , allpar ) ;
       INFO_message("allcost output: start fine #%d",kk) ;
       for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
         fprintf(stderr,"   %-4s = %g\n",meth_shortname[jj],allcost->ar[jj]) ;
       KILL_floatvec(allcost) ;
       if( save_hist != NULL ) SAVEHIST("allcost_finestart",0) ;
     }

     if( verb > 1 ){
       ININFO_message("- Initial  cost = %f",cost_ini) ;
       PARINI("- Initial fine") ;
     }

     powell_set_mfac( powell_mm , powell_aa ) ;  /* 07 Jun 2011 */
     nfunc = 0 ;

     /*-- start with some optimization with linear interp, for speed? --*/

     if( num_rtb == 0 &&
         (MRI_HIGHORDER(interp_code) || npt_match > 999999) ){
       float pini[MAXPAR] ;
       stup.interp_code = MRI_LINEAR ;
       stup.npt_match   = MIN(499999,npt_match) ;
       mri_genalign_scalar_setup( NULL,NULL,NULL, &stup ) ;
       if( verb > 1 ) ININFO_message("- start Intermediate optimization") ;
       /*** if( verb > 2 ) GA_do_params(1) ; ***/

       nfunc = mri_genalign_scalar_optim( &stup, rad, 0.0666*rad, 333 );

       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
         pini[jj] = stup.wfunc_param[jj].val_init ;
         stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out ;
       }

       stup.interp_code = interp_code ;  /* check cost of result with  */
       stup.npt_match   = npt_match ;    /* actual final interp method */
       mri_genalign_scalar_setup( NULL,NULL,NULL, &stup ) ;
       cost = mri_genalign_scalar_cost( &stup , NULL ) ; /* interp_code, not LINEAR */
       if( cost > cost_ini ){   /* should not happen, but it could since  */
         if( verb > 1 )         /* LINEAR cost optimized above isn't same */
           ININFO_message("- Intrmed  cost = %f > Initial cost = %f :-(",cost,cost_ini);
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           stup.wfunc_param[jj].val_init = pini[jj] ;
       } else {
         if( verb > 1 ){
           PARINI("- Intrmed fine") ;
           ININFO_message("- Intrmed  cost = %f ; %d funcs",cost,nfunc) ;
         }
         if( nfunc < 333 ){
           rad *= 0.246f ; if( rad < 9.99f*conv_rad ) rad = 9.99f*conv_rad ;
         }
       }

       if( do_allcost != 0 ){  /*-- all cost functionals for fun again --*/
         PAR_CPY(val_init) ;   /* copy init parameters into allpar[] */
         allcost = mri_genalign_scalar_allcosts( &stup , allpar ) ;
         INFO_message("allcost output: intermed fine #%d",kk) ;
         for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
           fprintf(stderr,"   %-4s = %g\n",meth_shortname[jj],allcost->ar[jj]) ;
         KILL_floatvec(allcost) ;
         if( save_hist != NULL ) SAVEHIST("allcost_fineintermed",0) ;
       }
     }

     /*-- now do the final final optimization, with the correct interp mode --*/

     /*** if( verb > 2 ) GA_do_params(1) ; ***/

     nfunc = mri_genalign_scalar_optim( &stup , rad, conv_rad,6666 );

     if( do_refinal ){  /*-- 14 Nov 2007: a final final optimization? --*/
       if( verb > 1 )
         ININFO_message("- Finalish cost = %f ; %d funcs",stup.vbest,nfunc) ;
       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out;
       if( verb > 1 ) PARINI("- ini Finalish") ;
       stup.need_hist_setup = 1 ;
       if( (meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
            meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) && micho_zfinal ){
         GA_setup_micho( 0.0 , 0.0 , 0.0 , 0.0 , 0.0 ) ;
         if( verb > 1 )
           ININFO_message(" - Set %s parameters back to purity before Final iterations",
                          meth_shortname[meth_code-1] ) ;
         rad *= 1.666f ;
       } else {
         rad *= 0.666f ;
       }
       if( powell_mm == 0.0f ) powell_set_mfac( 3.0f , 3.0f ) ;  /* 07 Jun 2011 */
       nfunc = mri_genalign_scalar_optim( &stup , rad, conv_rad,6666 );
       powell_set_mfac( powell_mm , powell_aa ) ;                /* 07 Jun 2011 */
     }

     /*** if( powell_mm > 0.0f ) powell_set_mfac( 0.0f , 0.0f ) ; ***/
     /*** if( verb > 2 ) GA_do_params(0) ; ***/

     /*** Optimzation is done, so do some cleanup and some output ***/

     if( verb > 1 ) ININFO_message("- Final    cost = %f ; %d funcs",stup.vbest,nfunc) ;
     if( verb > 1 || (verb==1 && kk==0) ) PARNOUT("Final fine fit") ; /* 30 Aug 2013 */
     if( verb > 1 ) ININFO_message("- Fine net CPU time = %.1f s",COX_cpu_time()-ctim) ;

     if( save_hist != NULL ) SAVEHIST("final",1) ;

     if( (meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
          meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) && micho_zfinal )  /* set them back */
       GA_setup_micho( micho_hel , micho_mi , micho_nmi , micho_crA , micho_ov ) ;

     if( do_allcost != 0 ){  /*-- all costs at final affine solution? --*/
       PAR_CPY(val_out) ;    /* copy output parameters into allpar[] */
       allcost = mri_genalign_scalar_allcosts( &stup , allpar ) ;
       INFO_message("allcost output: final fine #%d",kk) ;
       for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
         fprintf(stderr,"   %-4s = %g\n",meth_shortname[jj],allcost->ar[jj]) ;
       KILL_floatvec(allcost) ;
       if( save_hist != NULL ) SAVEHIST("allcost_finefinal",0) ;
     }

     if( do_save_pearson_map ){  /*-- Save Pearson map [25 Jan 2021] --*/
       SAVE_PEARSON_MAP(save_pearson_prefix,val_out) ;
       do_save_pearson_map = 0 ;
#if 0
       MRI_IMAGE *pim ;
       PAR_CPY(val_out) ;    /* copy output parameters into allpar[] */
       pim = mri_genalign_map_pearson_local( &stup , allpar ) ;
       if( pim != NULL ){
         THD_3dim_dataset *pset ;
         pset = THD_volume_to_dataset( dset_base , pim ,
                                       save_pearson_prefix ,
                                       pad_xm,pad_xp,pad_ym,pad_yp,pad_zm,pad_zp ) ;
         mri_free(pim) ;
         if( pset != NULL ){
           DSET_write(pset) ; WROTE_DSET(pset) ; DSET_delete(pset) ;
         } else {
           ERROR_message("Failed to create -PearSave dataset :(") ;
         }
       } else {
           ERROR_message("Failed to create -PearSave volume :(") ;
       }
#endif
     }

#if 0
     for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
       stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out ;
     mri_genalign_verbose(9) ;
     cost = mri_genalign_scalar_cost( &stup , NULL ) ;
     INFO_message("Recomputed final cost = %g",cost) ;
     if( verb > 1 ) mri_genalign_verbose(verb-1) ;
#endif

     /*----------------------------------------------------------------------*/
     /*------------ Nonlinear warp improvement to the above results? --------*/
     /*----------- Someday soon (?), this code will be expunged! ------------*/

/* macro to (re)setup some parameters in the work below */

#define PARAM_SETUP(pp,ff,vv)                                                          \
 do{ if( ff ){ stup.wfunc_param[pp].fixed = ff; stup.wfunc_param[pp].val_fixed = vv; } \
     else    { stup.wfunc_param[pp].fixed = 0; }                                       \
     stup.wfunc_param[pp].val_init = vv;                                               \
 } while(0)

     if( nwarp_pass ){

       /* 15 Dec 2010: change cost functional here? */

       if( nwarp_meth_code > 0 ){
         if( verb ) INFO_message( "-nwarp setup: switch method to '%s' from '%s'",
                                  meth_shortname[nwarp_meth_code-1] ,
                                  meth_shortname[stup.match_code-1]  ) ;
         stup.match_code = nwarp_meth_code ;
       }

       /*--- different blocks of code for the different types of warps ---*/

       if( nwarp_type == WARP_BILINEAR ){  /*------ special case [old] ------*/

         float rr , xcen,ycen,zcen , brad,crad ; int nbf , nite ;

         rr = MAX(xsize,ysize) ; rr = MAX(zsize,rr) ; rr = 1.2f / rr ;

         SETUP_BILINEAR_PARAMS ;  /* nonlinear params */

         /* nonlinear transformation is centered at middle of base volume
            indexes (xcen,ycen,zcen) and is scaled by reciprocal of size (rr) */

         MAT44_VEC( stup.base_cmat,
                    0.5f*nx_base, 0.5f*ny_base, 0.5f*nz_base,
                    xcen        , ycen        , zcen         ) ;
         stup.wfunc_param[NPBIL  ].val_fixed = stup.wfunc_param[NPBIL  ].val_init = xcen;
         stup.wfunc_param[NPBIL+1].val_fixed = stup.wfunc_param[NPBIL+1].val_init = ycen;
         stup.wfunc_param[NPBIL+2].val_fixed = stup.wfunc_param[NPBIL+2].val_init = zcen;
         stup.wfunc_param[NPBIL+3].val_fixed = stup.wfunc_param[NPBIL+3].val_init = rr  ;

         /* affine part is copied from results of work thus far */

         for( jj=0 ; jj < 12 ; jj++ )
           stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out;

         stup.need_hist_setup = 1 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL, &stup );

         /* do the first pass of the bilinear optimization */

         if( verb > 0 ) INFO_message("Start bilinear warping") ;
         if( verb > 1 ) PARINI("- Bilinear initial") ;
         for( jj=12 ; jj <= 14 ; jj++ ){
           stup.wfunc_param[jj   ].fixed = 0 ;  /* just free up diagonal */
           stup.wfunc_param[jj+12].fixed = 0 ;  /* elements of B tensor */
           stup.wfunc_param[jj+24].fixed = 0 ;
         }
         if( verb ) ctim = COX_cpu_time() ;
         brad = MAX(conv_rad,0.001f) ;
              if( rad > 55.5f*brad ) rad = 55.5f*brad ;
         else if( rad < 22.2f*brad ) rad = 22.2f*brad ;
         crad = ( (nwarp_flags&1) == 0 ) ? (11.1f*brad) : (2.22f*brad) ;
         nite = MAX(555,nwarp_itemax) ;
         nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
         if( verb ){
           dtim = COX_cpu_time() ;
           ININFO_message("- Bilinear#1 cost = %f ; %d funcs ; net CPU = %.1f s",
                          stup.vbest,nbf,dtim-ctim) ;
           ctim = dtim ;
         }

         /* do the second pass, with more parameters varying */

         if( (nwarp_flags&1) == 0 ){
           float dnor , onor ;

           for( jj=0  ; jj < NPBIL ; jj++ )
             stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out;
#if 1
           for( jj=0 ; jj < 12 ; jj++ ){      /* fix affine params */
             stup.wfunc_param[jj].val_fixed = stup.wfunc_param[jj].val_out ;
             stup.wfunc_param[jj].fixed = 1 ;
           }
#endif
           for( jj=12 ; jj < NPBIL ; jj++ )   /* now free up all B elements */
             stup.wfunc_param[jj].fixed = 0 ;
           nite = MAX(1111,nwarp_itemax) ;
           nbf  = mri_genalign_scalar_optim( &stup, 33.3f*brad, 2.22f*brad,nite );
           if( verb ){
             dtim = COX_cpu_time() ;
             ININFO_message("- Bilinear#2 cost = %f ; %d funcs ; net CPU = %.1f s",
                            stup.vbest,nbf,dtim-ctim) ;
             ctim = dtim ;
           }

           /* run it again to see if it improves any more? */

           dnor = BILINEAR_diag_norm   (stup) ;
           onor = BILINEAR_offdiag_norm(stup) ;
           if( onor > 0.0333f * dnor ){
             for( jj=0  ; jj < NPBIL ; jj++ )
               stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out;
             nbf = mri_genalign_scalar_optim( &stup, 4.44f*brad, brad, 222 );
             if( verb ){
               dtim = COX_cpu_time() ;
               ININFO_message("- Bilinear#3 cost = %f ; %d funcs ; net CPU = %.1f s",
                              stup.vbest,nbf,dtim-ctim) ;
               ctim = dtim ;
             }
           }
         }
         /** if( verb > 1 ) PAROUT("- Bilinear final") ; **/

         strcpy(warp_code_string,"bilinear") ;

       } else if( nwarp_type == WARP_CUBIC ){  /*------ special case ------------*/

         float rr , xcen,ycen,zcen , brad,crad ; int nbf , nite ;

         rr = MAX(xsize,ysize) ; rr = MAX(zsize,rr) ; rr = 1.2f / rr ;

         SETUP_CUBIC_PARAMS ;  /* nonlinear params */

         /* nonlinear transformation is centered at middle of base volume
            indexes (xcen,ycen,zcen) and is scaled by reciprocal of size (rr) */

         MAT44_VEC( stup.base_cmat,
                    0.5f*nx_base, 0.5f*ny_base, 0.5f*nz_base,
                    xcen        , ycen        , zcen         ) ;
         stup.wfunc_param[NPCUB  ].val_fixed = stup.wfunc_param[NPCUB  ].val_init = xcen;
         stup.wfunc_param[NPCUB+1].val_fixed = stup.wfunc_param[NPCUB+1].val_init = ycen;
         stup.wfunc_param[NPCUB+2].val_fixed = stup.wfunc_param[NPCUB+2].val_init = zcen;
         stup.wfunc_param[NPCUB+3].val_fixed = stup.wfunc_param[NPCUB+3].val_init = rr  ;

         /* affine part is copied from results of work thus far */

         for( jj=0 ; jj < 12 ; jj++ ){
           nbf = (stup.wfunc_param[jj].fixed) ? stup.wfunc_param[jj].fixed : nwarp_fixaff ;
           PARAM_SETUP( jj , nbf , stup.wfunc_param[jj].val_out ) ;
         }

         stup.need_hist_setup = 1 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL, &stup );

         /* do the optimization */

         /** if( verb > 1 ) PARINI("- Cubic/Poly3 initial") ; **/
         for( jj=12 ; jj < NPCUB  ; jj++ ) stup.wfunc_param[jj].fixed = 0 ;
         FREEZE_POLYNO_PARAMS ; /* 07 Dec 2010 */

         COUNT_FREE_PARAMS(nbf) ;
         if( verb > 0 )
           INFO_message("Start Cubic/Poly3 warping: %d free parameters",nbf) ;

         if( verb ) ctim = COX_cpu_time() ;
         rad  = 0.01f ; crad = 0.003f ;
         nite = MAX(2222,nwarp_itemax) ;
         nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
         if( verb ){
           dtim = COX_cpu_time() ;
           ININFO_message("- Cubic/Poly3 cost = %f ; %d funcs ; net CPU = %.1f s",
                          stup.vbest,nbf,dtim-ctim) ;
           ctim = dtim ;
         }

         /** if( verb > 1 ) PAROUT("- Cubic/Poly3 final") ; **/
         strcpy(warp_code_string,"cubic") ;

       } else if( nwarp_type == WARP_QUINT ){  /*------ special case ------------*/

         float rr , xcen,ycen,zcen , brad,crad ; int nbf , nite ;

         rr = MAX(xsize,ysize) ; rr = MAX(zsize,rr) ; rr = 1.2f / rr ;

         SETUP_QUINT_PARAMS ;  /* nonlinear params */

         /* nonlinear transformation is centered at middle of base volume
            indexes (xcen,ycen,zcen) and is scaled by reciprocal of size (rr) */

         MAT44_VEC( stup.base_cmat,
                    0.5f*nx_base, 0.5f*ny_base, 0.5f*nz_base,
                    xcen        , ycen        , zcen         ) ;
         PARAM_SETUP( NPQUINT   , 2 , xcen ) ;
         PARAM_SETUP( NPQUINT+1 , 2 , ycen ) ;
         PARAM_SETUP( NPQUINT+2 , 2 , zcen ) ;
         PARAM_SETUP( NPQUINT+3 , 2 , rr   ) ;

         /* affine part is copied from results of work thus far */

         for( jj=0 ; jj < 12 ; jj++ ){
           nbf = (stup.wfunc_param[jj].fixed) ? stup.wfunc_param[jj].fixed : nwarp_fixaff ;
           PARAM_SETUP( jj , nbf , stup.wfunc_param[jj].val_out ) ;
         }

         stup.need_hist_setup = 1 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL, &stup );
         GA_set_nperval(0) ;

         /* do the optimization */

         for( jj=12 ; jj < NPQUINT ; jj++ ) stup.wfunc_param[jj].fixed = 0 ;
         FREEZE_POLYNO_PARAMS ; /* 07 Dec 2010 */

         COUNT_FREE_PARAMS(nbf) ;
         if( verb > 0 )
           INFO_message("Start Quintic/Poly5 warping: %d free parameters",nbf) ;

         if( verb ) ctim = COX_cpu_time() ;
         rad  = 0.01f ; crad = 0.003f ;
         nite = MAX(3333,nwarp_itemax) ;
         nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
         if( verb ){
           dtim = COX_cpu_time() ;
           ININFO_message("- Quintic/Poly5 cost = %f ; %d funcs ; net CPU = %.1f s",
                          stup.vbest,nbf,dtim-ctim) ;
           ctim = dtim ;
         }

         /** if( verb > 1 ) PAROUT("- Quintic/Poly5 final") ; **/
         strcpy(warp_code_string,"quintic") ;

       } else if( nwarp_type == WARP_HEPT ){  /*------ special case ------------*/

         float rr , xcen,ycen,zcen , brad,crad ; int nbf , nite ;

         rr = MAX(xsize,ysize) ; rr = MAX(zsize,rr) ; rr = 1.2f / rr ;

         SETUP_HEPT_PARAMS ;  /* nonlinear params */

         /* nonlinear transformation is centered at middle of base volume
            indexes (xcen,ycen,zcen) and is scaled by reciprocal of size (rr) */

         MAT44_VEC( stup.base_cmat,
                    0.5f*nx_base, 0.5f*ny_base, 0.5f*nz_base,
                    xcen        , ycen        , zcen         ) ;
         PARAM_SETUP( NPHEPT   , 2 , xcen ) ;
         PARAM_SETUP( NPHEPT+1 , 2 , ycen ) ;
         PARAM_SETUP( NPHEPT+2 , 2 , zcen ) ;
         PARAM_SETUP( NPHEPT+3 , 2 , rr   ) ;

         /* affine part is copied from results of work thus far */

         for( jj=0 ; jj < 12 ; jj++ ){
           nbf = (stup.wfunc_param[jj].fixed) ? stup.wfunc_param[jj].fixed : nwarp_fixaff ;
           PARAM_SETUP( jj , nbf , stup.wfunc_param[jj].val_out ) ;
         }

         stup.need_hist_setup = 1 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL, &stup );
         GA_set_nperval(0) ;

         /* do the optimization */

         for( jj=12 ; jj < NPHEPT ; jj++ ) stup.wfunc_param[jj].fixed = 0 ;
         FREEZE_POLYNO_PARAMS ; /* 07 Dec 2010 */

         COUNT_FREE_PARAMS(nbf) ;
         if( verb > 0 )
           INFO_message("Start Heptic/Poly7 warping: %d free parameters",nbf) ;

         if( verb ) ctim = COX_cpu_time() ;
         rad  = 0.01f ; crad = 0.003f ;
         nite = MAX(4444,nwarp_itemax) ;
         nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
         if( verb ){
           dtim = COX_cpu_time() ;
           ININFO_message("- Heptic/Poly7 cost = %f ; %d funcs ; net CPU = %.1f s",
                          stup.vbest,nbf,dtim-ctim) ;
           ctim = dtim ;
         }

         /** if( verb > 1 ) PAROUT("- Heptic/Poly7 final") ; **/
         strcpy(warp_code_string,"heptic") ;

       } else if( nwarp_type == WARP_NONI ){  /*------ special case ------------*/

         float rr , xcen,ycen,zcen , brad,crad ; int nbf , nite ;

         rr = MAX(xsize,ysize) ; rr = MAX(zsize,rr) ; rr = 1.2f / rr ;

         SETUP_NONI_PARAMS ;  /* nonlinear params */

         /* nonlinear transformation is centered at middle of base volume
            indexes (xcen,ycen,zcen) and is scaled by reciprocal of size (rr) */

         MAT44_VEC( stup.base_cmat,
                    0.5f*nx_base, 0.5f*ny_base, 0.5f*nz_base,
                    xcen        , ycen        , zcen         ) ;
         PARAM_SETUP( NPNONI   , 2 , xcen ) ;
         PARAM_SETUP( NPNONI+1 , 2 , ycen ) ;
         PARAM_SETUP( NPNONI+2 , 2 , zcen ) ;
         PARAM_SETUP( NPNONI+3 , 2 , rr   ) ;

         /* affine part is copied from results of work thus far */

         for( jj=0 ; jj < 12 ; jj++ ){
           nbf = (stup.wfunc_param[jj].fixed) ? stup.wfunc_param[jj].fixed : nwarp_fixaff ;
           PARAM_SETUP( jj , nbf , stup.wfunc_param[jj].val_out ) ;
         }

         stup.need_hist_setup = 1 ;
         mri_genalign_scalar_setup( NULL,NULL,NULL, &stup );
         GA_set_nperval(0) ;

         /* do the optimization */

         powell_set_mfac( 1.2f , 5.0f ) ;

         if( AFNI_noenv("AFNI_NONIC_GRADUAL") ){  /* old way: all params at once */
           for( jj=12 ; jj < NPNONI ; jj++ ) stup.wfunc_param[jj].fixed = 0 ;
           FREEZE_POLYNO_PARAMS ; /* 07 Dec 2010 */
           COUNT_FREE_PARAMS(nbf) ;
           if( verb > 0 )
             INFO_message("Start Nonic/Poly9 warping: %d free parameters",nbf) ;

           if( verb ) ctim = COX_cpu_time() ;
           rad  = 0.03f ; crad = 0.003f ;
           nite = MAX(7777,nwarp_itemax) ;
           nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
           if( verb ){
             dtim = COX_cpu_time() ;
             ININFO_message("- Nonic/Poly9 cost = %f ; %d funcs ; net CPU = %.1f s",
                            stup.vbest,nbf,dtim-ctim) ;
             ctim = dtim ;
           }
         } else { /* the new way: cubic, then quintic, then heptic, then nonic */
#undef  NPOL
#define NPOL(k) (((k)+1)*((k)+2)*((k)+3)/6-4)
           static int fst[8] = { 12+3*NPOL(3) , 12+3*NPOL(4) , 12+3*NPOL(5) ,
                                 12+3*NPOL(6) , 12+3*NPOL(7) , 12+3*NPOL(8) ,
                                 12+3*NPOL(9)  } ;
           int pq , ngite=2 , ig ; char *eee ; float gfac ;
           eee = my_getenv("AFNI_NONIC_GRADUAL") ;
           if( eee != NULL && isdigit(*eee) ) ngite = (int)strtod(eee,NULL) ;
           for( ig=0 ; ig < ngite ; ig++ ){
             if( verb > 0 )
               INFO_message("Start iteration #%d/%d of Nonic/Poly9 gradual warp",ig+1,ngite) ;
             for( pq=0 ; pq < 7 ; pq++ ){
               for( jj=12 ; jj < NPNONI ; jj++ ) stup.wfunc_param[jj].fixed = 0 ;
               FREEZE_POLYNO_PARAMS ;
               for( jj=fst[pq] ; jj < NPNONI ; jj++ )
                 if( stup.wfunc_param[jj].fixed == 0 ) stup.wfunc_param[jj].fixed = 1 ;
               COUNT_FREE_PARAMS(nbf) ;
               if( verb > 0 )
                 ININFO_message("Level %d of Nonic/Poly9 warping: %d free parameters",pq+3,nbf) ;
               if( nbf == 0 ) continue ;
               if( pq > 0 || ig > 0 ){
                 for( jj=0 ; jj < fst[pq] ; jj++ ){
                   if( stup.wfunc_param[jj].fixed == 0 )
                     stup.wfunc_param[jj].val_init = stup.wfunc_param[jj].val_out ;
                 }
               }
               if( verb ) ctim = COX_cpu_time() ;
               gfac = 1.0f / sqrtf(ig+1.0f) ;
               rad  = 0.05f*gfac ; crad = 0.003f*gfac ;
               nite = MAX(19*nbf,nwarp_itemax) ;
               nbf  = mri_genalign_scalar_optim( &stup , rad, crad, nite );
               for( jj=0 ; jj < NPNONI ; jj++ ){        /* for fixers next time thru */
                 if( stup.wfunc_param[jj].fixed == 0 )
                   stup.wfunc_param[jj].val_fixed = stup.wfunc_param[jj].val_out ;
               }
               if( verb ){
                 dtim = COX_cpu_time() ;
                 ININFO_message("- Nonic/Poly9 cost = %f ; %d funcs ; net CPU = %.1f s",
                                stup.vbest,nbf,dtim-ctim) ;
                 ctim = dtim ;
               }
             } /* end of pq loop */
           } /* end of ig loop */
         } /* end of GRADUAL-osity */

         /** if( verb > 1 ) PAROUT("- Nonic/Poly9 final") ; **/
         strcpy(warp_code_string,"nonic") ;

       } else {   /*-------- unimplemented ----------*/

         ERROR_message("Unknown nonlinear warp type!") ;

       } /* end of Warpfield */

     } /* end of nonlinear warp */

     /*-------- FINALLY HAVE FINISHED -----------------------------*/

     mri_free(im_targ) ; im_targ = NULL ;

#ifdef ALLOW_METH_CHECK
     /*--- 27 Sep 2006: check if results are stable when
                        we optimize a different cost functional ---*/

     if( meth_check_count > 0 ){
       float pval[MAXPAR] , pdist , dmax ; int jmax,jtop ;
       float **aval = NULL ;
       int mm , mc ;

       if( meth_check_count > 1 ){   /* save for median-izing at end */
         aval = (float **)malloc(sizeof(float *)*stup.wfunc_numpar) ;
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
           aval[jj] = (float *)malloc(sizeof(float)*(meth_check_count+1)) ;
           aval[jj][0] = stup.wfunc_param[jj].val_out ;
         }
       }

       PAROUT("Final fit") ;
       INFO_message("Checking %s (%s) vs other costs",
                    meth_longname[meth_code-1] , meth_shortname[meth_code-1] ) ;
       for( mm=0 ; mm < meth_check_count ; mm++ ){
         mc = meth_check[mm] ; if( mc <= 0 ) continue ;
         if( verb > 1 ){
           ININFO_message("- checking vs cost %s (%s)",
                          meth_longname[mc-1],meth_shortname[mc-1]) ;
           ctim = COX_cpu_time() ;
         }
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ) /* save output params */
           stup.wfunc_param[jj].val_init = pval[jj] = stup.wfunc_param[jj].val_out;

         stup.match_code = mc ;
         nfunc = mri_genalign_scalar_optim( &stup, 33.3*conv_rad, conv_rad,666 );
         stup.match_code = meth_code ;

         if( aval != NULL ){
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
             aval[jj][mm+1] = stup.wfunc_param[jj].val_out ;
         }

         /* compute distance between 2 output parameter sets */

         jtop = MIN( 9 , stup.wfunc_numpar ) ; jmax = 0 ;
         for( dmax=0.0f,jj=0 ; jj < jtop ; jj++ ){
           if( !stup.wfunc_param[jj].fixed ){
             pdist = fabsf( stup.wfunc_param[jj].val_out - pval[jj] )
                    /(stup.wfunc_param[jj].max-stup.wfunc_param[jj].min) ;
             if( pdist > dmax ){ dmax = pdist ; jmax = jj ; }
           }
         }

         if( dmax > 20.0*conv_rad )
           WARNING_message(
             "Check vs %s (%s): max parameter discrepancy=%.4f%%! tolerance=%.4f%%",
             meth_longname[mc-1] , meth_shortname[mc-1] , 100.0*dmax , 2000.0*conv_rad ) ;
         else
           ININFO_message(
             "INFO:   Check vs %s (%s): max parameter discrepancy=%.4f%% tolerance=%.4f%%",
             meth_longname[mc-1] , meth_shortname[mc-1] , 100.0*dmax , 2000.0*conv_rad ) ;
         PAROUT("Check fit") ;
         if( verb > 1 )
           ININFO_message("- Check net CPU time=%.1f s; funcs=%d; dmax=%f jmax=%d",
                          COX_cpu_time()-ctim , nfunc , dmax , jmax ) ;
         if( do_allcost != 0 ){
           PAR_CPY(val_out) ;  /* copy output parameters into allpar */
           allcost = mri_genalign_scalar_allcosts( &stup , allpar ) ;
           ININFO_message("allcost output: check %s",meth_shortname[mc-1]) ;
           for( jj=0 ; jj < GA_MATCH_METHNUM_SCALAR ; jj++ )
             fprintf(stderr,"   %-4s = %g\n",meth_shortname[jj],allcost->ar[jj]) ;
           KILL_floatvec(allcost) ;
           if( save_hist != NULL ){
             char fn[64] ; sprintf(fn,"allcost_check_%s",meth_shortname[mc-1]);
             SAVEHIST(fn,0);
           }
         }

         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           stup.wfunc_param[jj].val_out = pval[jj] ;  /* restore previous param */
       } /* end of loop over check methods */

       if( aval != NULL ){  /* median-ize the parameter sets */
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ){
           pval[jj] = qmed_float( meth_check_count+1 , aval[jj] ) ;
           free((void *)aval[jj]) ;
         }
         free((void *)aval) ;
         fprintf(stderr," + Median of Parameters =") ;
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ ) fprintf(stderr," %.6f",pval[jj]) ;
         fprintf(stderr,"\n") ;
         if( meth_median_replace ){  /* replace final results with median! */
           ININFO_message("Replacing Final parameters with Median") ;
           for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
             stup.wfunc_param[jj].val_out = pval[jj] ;
         }
       }

     } /* end of checking */
#endif

     /*- freeze warp-ing parameters (those after #0..5) for later rounds */

     if( warp_freeze && DSET_NVALS(dset_targ) > 1 ){  /* 10 Oct 2006 */
       for( jj=6 ; jj < stup.wfunc_numpar ; jj++ ){
         if( !stup.wfunc_param[jj].fixed ){
           if( verb > 1 ) INFO_message("Freezing parameter #%d [%s] = %.6f",
                                       jj+1 , stup.wfunc_param[jj].name ,
                                              stup.wfunc_param[jj].val_out ) ;
           stup.wfunc_param[jj].fixed = 2 ;
           stup.wfunc_param[jj].val_fixed = stup.wfunc_param[jj].val_out ;
         }
       }
     }

     /*--- do we replace the base image with warped first target image? ---*/

     if( replace_base ){
       float pp[MAXPAR] ; MRI_IMAGE *aim ;
       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         pp[jj] = stup.wfunc_param[jj].val_out ;
       mri_free(im_base) ;
       if( verb > 1 ) INFO_message("Computing replacement base image") ;
       aim = (stup.ajimor != NULL) ? stup.ajimor : stup.ajim ;
       im_base =
        im_bset = mri_genalign_scalar_warpone(
                             stup.wfunc_numpar , pp , stup.wfunc ,
                             aim, nx_base,ny_base,nz_base, final_interp );
#if 0
       im_wset = im_weig ;  /* not needed, since stup 'remembers' the weight */
#endif
       replace_base = 0 ; diffblur = 0 ;
     }
     if( replace_meth ){
       if( verb > 1 ) INFO_message("Replacing meth='%s' with '%s'",
                                   meth_shortname[meth_code] ,
                                   meth_shortname[replace_meth] ) ;
       meth_code = replace_meth; replace_meth = 0;
     }

     /*-- get final DICOM coord transformation matrix [23 Jul 2007] --*/

     { float par[MAXPAR] ;
       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         par[jj] = stup.wfunc_param[jj].val_out ;
#if 0
mri_genalign_set_pgmat(1) ;
#endif
       mri_genalign_affine( stup.wfunc_numpar , par , 0,NULL,NULL,NULL , NULL,NULL,NULL ) ;
       mri_genalign_affine_get_gammaijk( &qmat ) ;
       wmat = MAT44_MUL(targ_cmat,qmat) ;    /* matrix multiplies to undo the befafter stuff */
       aff12_xyz = MAT44_MUL(wmat,base_cmat_inv) ;  /* DICOM coord matrix */
     }

#if 0
DUMP_MAT44("targ_cmat",targ_cmat) ;
DUMP_MAT44("targ_cmat_inv",targ_cmat_inv) ;
DUMP_MAT44("base_cmat",base_cmat) ;
DUMP_MAT44("base_cmat_inv",base_cmat_inv) ;
DUMP_MAT44("aff12_xyz",aff12_xyz) ;
DUMP_MAT44("aff12_ijk",qmat) ;
#endif

     /*--- at this point, val_out contains alignment parameters ---*/

   WRAP_IT_UP_BABY: /***** goto target !!!!! *****/

     /* save parameters for the historical record */

     if( parsave != NULL ){
       parsave[kk] = (float *)malloc(sizeof(float)*stup.wfunc_numpar) ;
       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         parsave[kk][jj] = stup.wfunc_param[jj].val_out ;
     }

     /* save matrix for the hysterical record [23 Jul 2007] */

     if( matsave != NULL ){
       if( ISVALID_MAT44(aff12_xyz) )
         matsave[kk] = aff12_xyz ;
       else
         LOAD_DIAG_MAT44(matsave[kk],1.0f,1.0f,1.0f) ;
     }

     /** store warped volume into the output dataset **/

     if( dset_out != NULL ){
       MRI_IMAGE *aim = (stup.ajimor != NULL) ? stup.ajimor : stup.ajim ;
       /* lose obliquity if using 3dWarp for any transformation */
       /* recompute Tc (Cardinal transformation matrix for new grid output */
       THD_make_cardinal(dset_out);    /* needed for oblique NIFTI datasets - 07/03/14 drg */

       if( verb > 1 ) INFO_message("Computing output image") ;
#if 0
mri_genalign_set_pgmat(1) ;
#endif

       switch( apply_mode ){
         default:
           AL_setup_warp_coords( epi_targ,epi_fe,epi_pe,epi_se,
                                 nxyz_dout, dxyz_dout, cmat_bout,
                                 nxyz_targ, dxyz_targ, cmat_tout ) ;

           if( im_targ_vector == NULL ){
             im_targ = mri_genalign_scalar_warpone(
                                   stup.wfunc_numpar , parsave[kk] , stup.wfunc ,
                                   aim , nxout,nyout,nzout, final_interp ) ;
           } else { /* RGB image [12 May 2020] */
             im_targ = mri_genalign_scalar_warpone(
                                   stup.wfunc_numpar , parsave[kk] , stup.wfunc ,
                                   im_targ_vector , nxout,nyout,nzout, final_interp ) ;
             mri_free(im_targ_vector) ; im_targ_vector = NULL ;
           }

           if( nwarp_save_prefix != NULL ){  /* 10 Dec 2010: save map of warp itself */
             THD_3dim_dataset *wset; MRI_IMARR *wimar;
             MRI_IMAGE *xim,*yim,*zim,*vim=NULL; int iv=0,nw;
             wimar = mri_genalign_scalar_xyzwarp(
                                 stup.wfunc_numpar , parsave[kk] , stup.wfunc ,
                                 nxout , nyout , nzout ) ;
             nw = IMARR_COUNT(wimar) ;
             wset = EDIT_empty_copy(dset_out) ;
             EDIT_dset_items( wset ,
                                ADN_prefix    , nwarp_save_prefix ,
                                ADN_nvals     , (twodim_code) ? nw-1 : nw ,
                                ADN_ntt       , 0 ,
                                ADN_datum_all , MRI_float ,
                              ADN_none ) ;
             xim = IMARR_SUBIM(wimar,0); yim = IMARR_SUBIM(wimar,1);
             zim = IMARR_SUBIM(wimar,2); if( nw == 4 ) vim = IMARR_SUBIM(wimar,3) ;
             FREE_IMARR(wimar) ;
             if( twodim_code != 1 ){
               EDIT_BRICK_LABEL( wset , iv , "x_delta" ) ;
               EDIT_substitute_brick(wset,iv,MRI_float,MRI_FLOAT_PTR(xim)) ;
               mri_clear_data_pointer(xim) ; iv++ ;
             }
             if( twodim_code != 2 ){
               EDIT_BRICK_LABEL( wset , iv , "y_delta" ) ;
               EDIT_substitute_brick(wset,iv,MRI_float,MRI_FLOAT_PTR(yim)) ;
               mri_clear_data_pointer(yim) ; iv++ ;
             }
             if( twodim_code != 3 ){
               EDIT_BRICK_LABEL( wset , iv , "z_delta" ) ;
               EDIT_substitute_brick(wset,iv,MRI_float,MRI_FLOAT_PTR(zim)) ;
               mri_clear_data_pointer(zim) ; iv++ ;
             }
             if( vim != NULL ){
               EDIT_BRICK_LABEL( wset , iv , "hexvol" ) ;
               EDIT_substitute_brick(wset,iv,MRI_float,MRI_FLOAT_PTR(vim)) ;
               mri_clear_data_pointer(vim) ; mri_free(vim) ; iv++ ;
             }
             mri_free(xim) ; mri_free(yim) ; mri_free(zim) ;
             DSET_write(wset) ; WROTE_DSET(wset) ; DSET_delete(wset) ;
           } /* end of nwarp_save */
         break ;

         case APPLY_AFF12:{
           float ap[12] ;
           wmat = MAT44_MUL(aff12_xyz,mast_cmat) ;
           qmat = MAT44_MUL(targ_cmat_inv,wmat) ;  /* index transform matrix */
           UNLOAD_MAT44_AR(qmat,ap) ;
           if( im_targ_vector == NULL ){
             im_targ = mri_genalign_scalar_warpone(
                                   12 , ap , mri_genalign_mat44 ,
                                   aim , nxout,nyout,nzout, final_interp ) ;
           } else { /* RGB image [12 May 2020] */
             im_targ = mri_genalign_scalar_warpone(
                                   12 , ap , mri_genalign_mat44 ,
                                   im_targ_vector , nxout,nyout,nzout, final_interp ) ;
             mri_free(im_targ_vector) ; im_targ_vector = NULL ;
           }
         }
         break ;
       }

       /* 04 Apr 2007: save matrix into dataset header */

       { static mat44 gam , gami ; char anam[64] ; float matar[12] , *psav ;

         if( matsave != NULL )
           gam = matsave[kk] ;
         else if( ISVALID_MAT44(aff12_xyz) )
           gam = aff12_xyz ;
         else
           mri_genalign_affine_get_gammaxyz( &gam ) ;  /* should not happen */

         if( ISVALID_MAT44(gam) ){
           sprintf(anam,"ALLINEATE_MATVEC_B2S_%06d",kk) ;
           UNLOAD_MAT44_AR(gam,matar) ;
           THD_set_float_atr( dset_out->dblk , anam , 12 , matar ) ;
           gami = MAT44_INV(gam) ;
           sprintf(anam,"ALLINEATE_MATVEC_S2B_%06d",kk) ;
           UNLOAD_MAT44_AR(gami,matar) ;
           THD_set_float_atr( dset_out->dblk , anam , 12 , matar ) ;
         }

         /* 22 Feb 2010: save parameters as well */

         if( kk == 0 && *warp_code_string != '\0' )
           THD_set_string_atr( dset_out->dblk , "ALLINEATE_WARP_TYPE" , warp_code_string ) ;

         psav = (float *)malloc(sizeof(float)*stup.wfunc_numpar) ;
         for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
           psav[jj] = stup.wfunc_param[jj].val_out ;
         sprintf(anam,"ALLINEATE_PARAMS_%06d",kk) ;
         THD_set_float_atr( dset_out->dblk , anam , stup.wfunc_numpar , psav ) ;
         free(psav) ;

       }

       /* save sub-brick without scaling factor */

       if( targ_was_vector ){                  /* RGB image [12 May 2020] */
         EDIT_substitute_brick( dset_out ,kk ,
                                im_targ->kind , mri_data_pointer(im_targ) ) ;
         mri_clear_data_pointer(im_targ) ;
       } else if( floatize || targ_kind == MRI_float ){
         EDIT_substitute_brick( dset_out,kk,MRI_float, MRI_FLOAT_PTR(im_targ) );
         mri_clear_data_pointer(im_targ) ;  /* data in im_targ saved directly */
       } else {
         EDIT_substscale_brick( dset_out,kk,MRI_float, MRI_FLOAT_PTR(im_targ),
                                targ_kind ,
                                (bfac == 0.0f) ? 1.0f : 0.0f ) ;
       }
       mri_free(im_targ) ; im_targ = NULL ;

       if( usetemp && DSET_NVALS(dset_out) > 1 )   /* 31 Jan 2007 */
         mri_purge( DSET_BRICK(dset_out,kk) ) ;
     }

     MEMORY_CHECK("end of sub-brick alignment") ;

   } /***------------- end of loop over target sub-bricks ------------------***/

   /*--- unload stuff we no longer need ---*/

   if( verb > 1 ) INFO_message("Unloading unneeded data") ;

   DSET_unload(dset_targ) ;
   mri_free(im_base); mri_free(im_weig); mri_free(im_mask);
#ifdef ALLOW_UNIFIZE
   mri_free(im_ubase);
#endif

   MRI_FREE(stup.bsim); MRI_FREE(stup.bsims);
   MRI_FREE(stup.ajim); MRI_FREE(stup.ajims); MRI_FREE(stup.bwght);
   MRI_FREE(stup.ajimor);

   /***--- write output dataset to disk? ---***/

   MEMORY_CHECK("end of sub-brick loop (after cleanup)") ;

   if( dset_out != NULL ){
     DSET_write(dset_out); WROTE_DSET(dset_out); DSET_unload(dset_out);
     MEMORY_CHECK("after writing output dataset") ;
   }

   /*--- save parameters to a file, if desired ---*/

   if( param_save_1D != NULL && parsave != NULL ){
     FILE *fp ;
     fp = (strcmp(param_save_1D,"-") == 0) ? stdout
                                           : fopen(param_save_1D,"w") ;
     if( fp == NULL ) ERROR_exit("Can't open -1Dparam_save %s for output!?",param_save_1D);
     fprintf(fp,"# 3dAllineate parameters:\n") ;
     fprintf(fp,"#") ;
     for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )         /* 04 Dec 2010 */
       fprintf(fp," %s%c" , stup.wfunc_param[jj].name ,  /* add '$' for frozen */
                (stup.wfunc_param[jj].fixed == 2) ? '$' : ' ' ) ;
     fprintf(fp,"\n") ;
     for( kk=0 ; kk < DSET_NVALS(dset_targ) ; kk++ ){
       for( jj=0 ; jj < stup.wfunc_numpar ; jj++ )
         fprintf(fp," %.6f",parsave[kk][jj]) ;
       fprintf(fp,"\n") ;                           /* oops */
     }
     if( fp != stdout ){
       fclose(fp) ; if( verb ) INFO_message("Wrote -1Dparam_save %s",param_save_1D) ;
     }
   }

   /*--- save matrices to disk, if so ordered by the omniscient user ---*/

   if( matrix_save_1D != NULL && matsave != NULL ){
     FILE *fp ;
     float a11,a12,a13,a14,a21,a22,a23,a24,a31,a32,a33,a34 ;
     fp = (strcmp(matrix_save_1D,"-") == 0) ? stdout
                                            : fopen(matrix_save_1D,"w") ;
     if( fp == NULL ) ERROR_exit("Can't open -1Dmatrix_save %s for output!?",matrix_save_1D);
     fprintf(fp,"# 3dAllineate matrices (DICOM-to-DICOM, row-by-row):\n") ;
     for( kk=0 ; kk < DSET_NVALS(dset_targ) ; kk++ ){
       UNLOAD_MAT44(matsave[kk],a11,a12,a13,a14,a21,a22,a23,a24,a31,a32,a33,a34) ;
       fprintf(fp,
               " %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g\n",
               a11,a12,a13,a14,a21,a22,a23,a24,a31,a32,a33,a34 ) ;
     }
     if( fp != stdout ){
       fclose(fp) ; if( verb ) INFO_message("Wrote -1Dmatrix_save %s",matrix_save_1D) ;
     }
   }

   /*---------- FREE AT LAST, FREE AT LAST ----------*/

   FREE_GA_setup(&stup) ;
   if( parsave != NULL ){
     for( kk=0 ; kk < DSET_NVALS(dset_targ) ; kk++ )
       if( parsave[kk] != NULL ) free((void *)parsave[kk]) ;
     free((void *)parsave) ;
   }
   if( matsave != NULL ) free((void *)matsave) ;

   if( 1 || verb )
     INFO_message("3dAllineate: total CPU time = %.1f sec  Elapsed = %.1f\n",
                  COX_cpu_time() , COX_clock_time() ) ;
   MEMORY_CHECK("end of program (after final cleanup)") ;
   if( verb && apply_1D == NULL && prefix != NULL ){
    INFO_message(  "###########################################################");
    INFO_message(  "#   Please check results visually for alignment quality   #");

    if( (meth_code == GA_MATCH_PEARSON_LOCALS   ||
         meth_code == GA_MATCH_PEARSON_LOCALA   ||
         meth_code == GA_MATCH_LPC_MICHO_SCALAR ||
         meth_code == GA_MATCH_LPA_MICHO_SCALAR   ) &&
        auto_weight != 1                              ){
      INFO_message("###########################################################");
      INFO_message("#   '-autoweight' is recommended when using -lpc or -lpa  #");
      INFO_message("#   If your results are not good, please try again.       #");
     }
   }
   if( verb ){
      INFO_message("###########################################################");
   }
   if( CMbad > 0 ){          /* 26 Feb 2020 */
     ININFO_message (" ") ;
     INFO_message   ("***********************************************************") ;
     WARNING_message("-cmass was turned off, but might have been needed :("       ) ;
     ININFO_message ("          please check your results - PLEASE PLEASE PLEASE" ) ;
     INFO_message   ("***********************************************************") ;
   }

   exit(0) ;
}

/*---------------------------------------------------------------------------*/
/*! Turn an input image into a weighting factor (for -autoweight).
    If acod == 2, then make a binary mask at the end.
    If acod == 3, then make a boxed binary mask at the end.
-----------------------------------------------------------------------------*/

MRI_IMAGE * mri_weightize( MRI_IMAGE *im, int acod, int ndil, float aclip, float apow )
{
   float *wf,clip,clip2 ;
   int xfade,yfade,zfade , nx,ny,nz,nxy,nxyz , ii,jj,kk,ff ;
   byte *mmm ;
   MRI_IMAGE *qim , *wim ;

   /*-- copy input image --*/

   qim = mri_to_float(im) ; wf = MRI_FLOAT_PTR(qim) ;
   nx = qim->nx; ny = qim->ny; nz = qim->nz; nxy = nx*ny; nxyz = nxy*nz;
   for( ii=0 ; ii < nxyz ; ii++ ) wf[ii] = fabsf(wf[ii]) ;

   /*-- zero out along the edges --*/
#undef  WW
#define WW(i,j,k) wf[(i)+(j)*nx+(k)*nxy]

   xfade = (int)(0.05*qim->nx+3.0) ;                 /* number of points */
   yfade = (int)(0.05*qim->ny+3.0) ;                 /* along each face */
   zfade = (int)(0.05*qim->nz+3.0) ;                 /* to set to zero */
   if( 5*xfade >= qim->nx ) xfade = (qim->nx-1)/5 ;
   if( 5*yfade >= qim->ny ) yfade = (qim->ny-1)/5 ;
   if( 5*zfade >= qim->nz ) zfade = (qim->nz-1)/5 ;
   if( verb > 1 )
     ININFO_message("Weightize: xfade=%d yfade=%d zfade=%d",xfade,yfade,zfade);
   for( jj=0 ; jj < ny ; jj++ )
    for( ii=0 ; ii < nx ; ii++ )
     for( ff=0 ; ff < zfade ; ff++ ) WW(ii,jj,ff) = WW(ii,jj,nz-1-ff) = 0.0f;
   for( kk=0 ; kk < nz ; kk++ )
    for( jj=0 ; jj < ny ; jj++ )
     for( ff=0 ; ff < xfade ; ff++ ) WW(ff,jj,kk) = WW(nx-1-ff,jj,kk) = 0.0f;
   for( kk=0 ; kk < nz ; kk++ )
    for( ii=0 ; ii < nx ; ii++ )
     for( ff=0 ; ff < yfade ; ff++ ) WW(ii,ff,kk) = WW(ii,ny-1-ff,kk) = 0.0f;

   if( aclip > 0.0f ){  /* 31 Jul 2007 */
     int nleft , nclip ;
     for( nclip=nleft=ii=0 ; ii < nxyz ; ii++ ){
       if( wf[ii] > 0.0f ){
         if( wf[ii] < aclip ){ nclip++; wf[ii] = 0.0f; } else nleft++ ;
       }
     }
     if( verb > 1 ) ININFO_message("Weightize: user clip=%g #clipped=%d #left=%d",
                                   aclip,nclip,nleft) ;
   }

   /*-- squash super-large values down to reasonability --*/

   clip = 3.0f * THD_cliplevel(qim,0.5f) ;
   if( verb > 1 ) ININFO_message("Weightize: (unblurred) top clip=%g",clip) ;
   for( ii=0 ; ii < nxyz ; ii++ ) if( wf[ii] > clip ) wf[ii] = clip ;

   /*-- blur a little: median then Gaussian;
          the idea is that the median filter smashes localized spikes,
          then the Gaussian filter does a litte extra general smoothing. --*/

   mmm = (byte *)malloc( sizeof(byte)*nxyz ) ;
   for( ii=0 ; ii < nxyz ; ii++ ) mmm[ii] = (wf[ii] > 0.0f) ; /* mask */
   if( wt_medsmooth > 0.0f ){
     wim = mri_medianfilter( qim , wt_medsmooth , mmm , 0 ) ; mri_free(qim) ;
   } else {
     wim = qim ;
   }
   wf = MRI_FLOAT_PTR(wim) ;
   if( wt_gausmooth > 0.0f )
     FIR_blur_volume_3d( wim->nx , wim->ny , wim->nz ,
                         1.0f , 1.0f , 1.0f ,  wf ,
                         wt_gausmooth , wt_gausmooth , wt_gausmooth ) ;

   /*-- clip off small values, and
        keep only the largest cluster of supra threshold voxels --*/

   clip  = 0.05f * mri_max(wim) ;
   clip2 = 0.33f * THD_cliplevel(wim,0.33f) ;
   clip  = MAX(clip,clip2) ;
   if( verb > 1 ) ININFO_message("Weightize: (blurred) bot clip=%g",clip) ;
   for( jj=ii=0 ; ii < nxyz ; ii++ ){
     if( wf[ii] >= clip ){ jj++ ; mmm[ii] = 1 ; }
     else                {        mmm[ii] = 0 ; }
   }
   if( verb > 1 ) ININFO_message("Weightize: %d voxels survive clip",jj) ;
   if( ! doing_2D ){                          /* 28 Apr 2020 */
     THD_mask_clust( nx,ny,nz, mmm ) ;
     THD_mask_erode( nx,ny,nz, mmm, 1, 2 ) ;  /* cf. thd_automask.c NN2 */
     THD_mask_clust( nx,ny,nz, mmm ) ;
   } else {
     THD_mask_remove_isolas( nx,ny,nz , mmm ) ;
   }
   for( jj=nxyz,ii=0 ; ii < nxyz ; ii++ ){
     if( !mmm[ii] ){ wf[ii] = 0.0f ; jj-- ; }
   }
   free((void *)mmm) ;
   if( verb > 1 ) ININFO_message("Weightize: %d voxels survive clusterize",jj) ;

   /*-- convert to 0..1 range [10 Sep 2007] --*/

   clip = 0.0f ;
   for( ii=0 ; ii < nxyz ; ii++ ) if( wf[ii] > clip ) clip = wf[ii] ;
   if( clip == 0.0f )
     ERROR_exit("Can't compute autoweight: max value seen as 0") ;
   clip = 1.0f / clip ;
   for( ii=0 ; ii < nxyz ; ii++ ) wf[ii] *= clip ;

   /*-- power? --*/

   if( apow > 0.0f && apow != 1.0f ){
     if( verb > 1 ) ININFO_message("Weightize: raising to %g power",apow) ;
     for( ii=0 ; ii < nxyz ; ii++ )
       if( wf[ii] > 0.0f ) wf[ii] = powf( wf[ii] , apow ) ;
   }

   /*-- binarize (acod==2)?  boxize (acod==3)? --*/

#undef  BPAD
#define BPAD 4
   if( acod == 2 || acod == 3 ){  /* binary weight: mask=2 or maskbox=3 */
     if( verb > 1 ) ININFO_message("Weightize: binarizing") ;
     for( ii=0 ; ii < nxyz ; ii++ ) if( wf[ii] != 0.0f ) wf[ii] = 1.0f ;
     if( ndil > 0 ){  /* 01 Mar 2007: dilation */
       byte *mmm = (byte *)malloc(sizeof(byte)*nxyz) ;
       if( verb > 1 ) ININFO_message("Weightize: dilating") ;
       for( ii=0 ; ii < nxyz ; ii++ ) mmm[ii] = (wf[ii] != 0.0f) ;
       for( ii=0 ; ii < ndil ; ii++ ){
         THD_mask_dilate     ( nx,ny,nz , mmm , 3, 2 ) ;
         THD_mask_fillin_once( nx,ny,nz , mmm , 2 ) ;
       }
       for( ii=0 ; ii < nxyz ; ii++ ) wf[ii] = (float)mmm[ii] ;
       free(mmm) ;
     }
     if( acod == 3 ){  /* boxize */
       int xm,xp , ym,yp , zm,zp ;
       MRI_autobbox_clust(0) ;
       MRI_autobbox( wim , &xm,&xp , &ym,&yp , &zm,&zp ) ;
       xm -= BPAD ; if( xm < 1    ) xm = 1 ;
       ym -= BPAD ; if( ym < 1    ) ym = 1 ;
       zm -= BPAD ; if( zm < 1    ) zm = 1 ;
       xp += BPAD ; if( xp > nx-2 ) xp = nx-2 ;
       yp += BPAD ; if( yp > ny-2 ) yp = ny-2 ;
       zp += BPAD ; if( zp > nz-2 ) zp = nz-2 ;
       if( verb > 1 )
         ININFO_message("Weightize: box=%d..%d X %d..%d X %d..%d = %d voxels",
                        xm,xp , ym,yp , zm,zp , (xp-xm+1)*(yp-ym+1)*(zp-zm+1) ) ;
       for( kk=zm ; kk <= zp ; kk++ )
        for( jj=ym ; jj <= yp ; jj++ )
         for( ii=xm ; ii <= xp ; ii++ ) WW(ii,jj,kk) = 1.0f ;
     }
   }

   return wim ;
}

/*---------------------------------------------------------------------------*/
/*! Return the L_infinity distance between two parameter vectors. */

float param_dist( GA_setup *stp , float *aa , float *bb )
{
   int jj ; float ap, bp, pdist, dmax ;

   if( stp == NULL || aa == NULL || bb == NULL ) return 1.0f ;

   dmax = 0.0f ;
   for( jj=0 ; jj < stp->wfunc_numpar ; jj++ ){
     if( !stp->wfunc_param[jj].fixed ){
       ap = aa[jj] ; bp = bb[jj] ;
       pdist = fabsf(ap-bp)
              / (stp->wfunc_param[jj].max - stp->wfunc_param[jj].min) ;
       if( pdist > dmax ) dmax = pdist ;
     }
   }
   return dmax ;
}

/*---------------------------------------------------------------------------*/
/*! Setup before and after index-to-coordinate matrices in the warp func.
    See the notes at the end of this file for the gruesome details.
-----------------------------------------------------------------------------*/

void AL_setup_warp_coords( int epi_targ , int epi_fe, int epi_pe, int epi_se,
                           int *nxyz_base, float *dxyz_base, mat44 base_cmat,
                           int *nxyz_targ, float *dxyz_targ, mat44 targ_cmat )
{
   mat44 cmat_before , imat_after , gmat,tmat,qmat ;
   float *dijk ; int *nijk ;

   if( epi_targ < 0 ){            /*---------- no EPI info given ----------*/

     /* [it] = inv[Ct] [S] [D] [U] [Cb]     [ib]
               ------- ----------- --------
               [after] [transform] [before]      */

     imat_after  = MAT44_INV(targ_cmat) ;  /* xyz -> ijk for target */
     cmat_before = base_cmat ;             /* ijk -> xyz for base */

   } else if( epi_targ == 1 ){  /*---------- target is EPI --------------*/

     dijk = dxyz_targ ; nijk = nxyz_targ ;

     /* -FPS kij should have           [ 0  0  dk -mk ]
                              [gmat] = [ di 0  0  -mi ]
                                       [ 0  dj 0  -mj ]
                                       [ 0  0  0   1  ]
        In this example, epi_fe=2, epi_pe=0, epi_se=1   */

     ZERO_MAT44(gmat) ;
     gmat.m[0][epi_fe] = dijk[epi_fe] ;
     gmat.m[1][epi_pe] = dijk[epi_pe] ;
     gmat.m[2][epi_se] = dijk[epi_se] ;
     gmat.m[0][3]      = -0.5f * dijk[epi_fe] * (nijk[epi_fe]-1) ;
     gmat.m[1][3]      = -0.5f * dijk[epi_pe] * (nijk[epi_pe]-1) ;
     gmat.m[2][3]      = -0.5f * dijk[epi_se] * (nijk[epi_se]-1) ;

     /* [it] = inv[Gt] [S] [D] [U] inv[Rt] [Cb] [ib]
               ------- ----------- ------------
               [after] [transform] [before]        where [Ct] = [Rt] [Gt] */

     imat_after  = MAT44_INV(gmat) ;
     tmat        = MAT44_INV(targ_cmat) ;       /* inv[Ct] */
     qmat        = MAT44_MUL(tmat,base_cmat) ;  /* inv[Ct] [Cb] */
     cmat_before = MAT44_MUL(gmat,qmat) ;       /* [G] inv[Ct] [Cb] */

   } else {                     /*---------- base is EPI ----------------*/

     dijk = dxyz_base ; nijk = nxyz_base ;

     ZERO_MAT44(gmat) ;
     gmat.m[0][epi_fe] = dijk[epi_fe] ;
     gmat.m[1][epi_pe] = dijk[epi_pe] ;
     gmat.m[2][epi_se] = dijk[epi_se] ;
     gmat.m[0][3]      = -0.5f * dijk[epi_fe] * (nijk[epi_fe]-1) ;
     gmat.m[1][3]      = -0.5f * dijk[epi_pe] * (nijk[epi_pe]-1) ;
     gmat.m[2][3]      = -0.5f * dijk[epi_se] * (nijk[epi_se]-1) ;

     /*  [it] = inv[Ct] [Rb] [U] [S] [D] [Gb]     [ib]
                ------------ ----------- --------
                [after]      [transform] [before]  where [Cb] = [Rb] [Gb] */

     cmat_before = gmat ;                       /* [Gb] */
     qmat        = MAT44_INV(gmat) ;            /* inv[Gb] */
     qmat        = MAT44_MUL(base_cmat,qmat) ;  /* [Cb] inv[Gb] = [Rb] */
     tmat        = MAT44_INV(targ_cmat) ;       /* inv[Ct] */
     imat_after  = MAT44_MUL(tmat,qmat) ;       /* inv[Ct] [Rb] */
   }

   /*-- actually let the warping function 'know' about these matrices --*/

   mri_genalign_affine_set_befafter( &cmat_before , &imat_after ) ;

   return ;
}

/* create MRI_IMAGE Identity parameters (not affine matrix) */
MRI_IMAGE * mri_identity_params(void)
{

   MRI_IMAGE *om;
   float id[] = {0,0,0, 0,0,0, 1,1,1, 0,0,0};
   float *oar;
   int ii;

   om  = mri_new( 12 , 1 , MRI_float ) ;
   oar = MRI_FLOAT_PTR(om) ;

   for( ii=0 ; ii < 12 ; ii++ )
         oar[ii] = id[ii] ;

   RETURN(om) ;
}

#ifdef USE_OMP
#include "mri_genalign_util.c"
#include "mri_genalign.c"
#include "thd_correlate.c"
#endif

/*----------------------------------------------------------------------------*/
#if 0
#undef  MMM
#define MMM(i,j,k) mmm[(i)+(j)*nx+(k)*nxy]

int * mri_edgesize( MRI_IMAGE *im )  /* 13 Aug 2007 */
{
   byte *mmm ;
   int ii,jj,kk , nx,ny,nz , nxy ;
   static int eijk[6] ;

ENTRY("mri_edgesize") ;

   if( im == NULL ) RETURN( NULL );

   nx = im->nx ; ny = im->ny ; nz = im->nz ; nxy = nx*ny ;

   STATUS("automask-ing on the cheap") ;

   THD_automask_set_cheapo(1) ;
   mmm = mri_automask_image( im ) ;
   if( mmm == NULL ) RETURN( NULL );

   /* check i-direction */

   STATUS("check i-direction") ;

   for( ii=0 ; ii < nx ; ii++ ){
     for( kk=0 ; kk < nz ; kk++ ){
       for( jj=0 ; jj < ny ; jj++ ) if( MMM(ii,jj,kk) ) goto I1 ;
   }}
 I1: eijk[0] = ii ;
   for( ii=nx-1 ; ii >= 0 ; ii-- ){
     for( kk=0 ; kk < nz ; kk++ ){
       for( jj=0 ; jj < ny ; jj++ ) if( MMM(ii,jj,kk) ) goto I2 ;
   }}
 I2: eijk[1] = nx-1-ii ;

   /* check j-direction */

   STATUS("check j-direction") ;

   for( jj=0 ; jj < ny ; jj++ ){
     for( kk=0 ; kk < nz ; kk++ ){
       for( ii=0 ; ii < nx ; ii++ ) if( MMM(ii,jj,kk) ) goto J1 ;
   }}
 J1: eijk[2] = jj ;
     for( jj=ny-1 ; jj >= 0 ; jj-- ){
       for( kk=0 ; kk < nz ; kk++ ){
         for( ii=0 ; ii < nx ; ii++ ) if( MMM(ii,jj,kk) ) goto J2 ;
     }}
 J2: eijk[3] = ny-1-jj ;

   /* check k-direction */

   STATUS("check k-direction") ;

   for( kk=0 ; kk < nz ; kk++ ){
     for( jj=0 ; jj < ny ; jj++ ){
       for( ii=0 ; ii < nx ; ii++ ) if( MMM(ii,jj,kk) ) goto K1 ;
   }}
 K1: eijk[4] = kk ;
   for( kk=nz-1 ; kk >= 0 ; kk-- ){
     for( jj=0 ; jj < ny ; jj++ ){
       for( ii=0 ; ii < nx ; ii++ ) if( MMM(ii,jj,kk) ) goto K2 ;
   }}
 K2: eijk[5] = nz-1-kk ;

   free(mmm) ; RETURN( eijk );
}
#endif

/******************************************************************************
*******************************************************************************

       ==============================================================
     ===== Notes on Coordinates and Indexes - RWCox - 05 Oct 2006 =====
       ==============================================================

The base and target datasets each have their own coordinate systems and
indexes.  We use 4x4 matrices to represent affine transformations, and
4-vectors to represent coordinates and indexes.  (The last row of a 4x4
matrix is [0 0 0 1] and the last element of a 4-vector is always 1.)
The index-to-coordinate transformations for base and target are given by

  [xb] = [Cb] [ib]
  [xt] = [Ct] [it]

where [Cb] and [Ct] are the dset->daxes->ijk_to_dicom matrices in the
datasets' header.

The 4x4 affine transformation matrix is not directly parametrized by its 12
non-trivial elements.  To give control over and meaning to the parameters,
the matrix is instead modeled as

  [T] = [S] [D] [U]

where [S] is a shear matrix, [D] is a diagonal scaling matrix, and [U] is
a proper orthogonal matrix.  If we wish to restrict the transformation [T]
to rigid body movement, for example, then we can fix the [S] and [D]
matrices to be the identity.

N.B.: The shift matrix [H] can be inserted before or after the [S][D][U]
product, as desired, so we'd really have [H][S][D][U] for dcode==DELTA_AFTER
and [S][D][U][H] for dcode==DELTA_BEFORE.  [H] must be a matrix of the form
   [ 1 0 0 a ]
   [ 0 1 0 b ]
   [ 0 0 1 c ]
   [ 0 0 0 1 ]
where {a,b,c} are the shifts.  I will ignore [H] in what follows.  Also,
the order [S][D][U] can be altered by the user, which I'll pretty much
ignore below, as well.

For EPI data, we may want to restrict the transformation parameters so as
to treat the phase-encoding direction differently than the frequency- and
slice-encoding directions.  However, the matrices as described above mean
that the [T] matrix components apply to DICOM coordinates, which may not
be aligned with the FPS directions in the image.  In such a case, putting
restrictions on the [T] parameters will not translate in a simple way into
FPS coordinates.

The solution is to break the transformation from indexes to spatial
coordinates into two pieces.  Let [C] = [R] [G], where [C] is an index-to
DICOM space matrix, [G] is a matrix that transforms indexes to FPS coordinates,
and [R] is "what's left" (should be a rotation matrix, possibly with
det[R]=-1).  A sample [G] is

        [ 0  0  dk -mk ]
  [G] = [ di 0  0  -mi ]
        [ 0  dj 0  -mj ]
        [ 0  0  0   1  ]

where the dataset is stored with '-FPS kij':
  i=P (phase-encoding), j=S (slice-encoding), and k=F (frequency-encoding);
  d{i,j,k} is the grid spacing along the respective dimensions; and
  m{i,j,k} is the coordinate at the volume center: e.g., mi=0.5*di*(ni-1).
(Here, 'i' refers to the first index in the dataset, etc.)

If we break up [Ct] this way, then the transformation is

  [xt] = [S] [D] [U] [xb], or
  [xb] = inv[U] inv[D] inv[S] [xt]
       = inv[U] inv[D] inv[S] [Rt] [Gt] [it]

and inv[T] is applied to the DICOM ordered coordinates in [Rt] [Gt] [it].
If we want to apply inv[T] to the FPS ordered coordinates, then we change
the last equation above to

  [xb] = [Rt] inv[U] inv[D] inv[S] [Gt] [it]

where inv[T] is now applied to coordinates where xyz=FPS.  Now, restricting
the parameters in [T] applies directly to FPS coordinates (e.g., fix the
scaling factor in [D] along the z-axis to 1, and then there will be no
stretching/shrinking of the data along the slice-encoding direction).  The
final multiplication by [Rt] rotates the inv[T]-transformed FPS coordinates
to DICOM order.

So, if the target dataset is the EPI dataset, then the transformation from
[ib] to [it] is expressed at

  [it] = inv[Gt] [S] [D] [U] inv[Rt] [Cb] [ib]
         ------- ----------- ------------
         [after] [transform] [before]

where the [transform] matrix is what the parameter searching is all about,
and the [before] and [after] matrices are fixed.

If the base dataset is the EPI dataset, on the other hand, then the index-
to-index transformation (what is really needed, after all) is

  [it] = inv[Ct] [Rb] [U] [D] [S] [Gb]     [ib]
         ------------ ----------- --------
         [after]      [transform] [before]

(N.B.: The SDU order has been inverted to UDS, on the presumption that
we will control the scaling and shear in the FPS coordinate system given
by [Gb][ib].) In the 'normal' case, where either (a) we are going to allow
full transform generality, or (b) no particular distortions of the image are
to be specially allowed for, then we simply have

  [it] = inv[Ct] [S] [D] [U] [Cb]     [ib]
         ------- ----------- --------
         [after] [transform] [before]

All of these cases are possible.  They will be especially important if/when
nonlinear warping is allowed in place of the simple [S][D][U] transformation,
the user needs to restrict the warping to occur only in the P-direction, and
the data slices are oblique.

*******************************************************************************
*******************************************************************************/
