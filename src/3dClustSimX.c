#include "mrilib.h"

#ifdef USE_OMP
#include <omp.h>
#endif

#include "mri_threshX.c"

/*---------------------------------------------------------------------------*/
/*
  Global data
*/

static THD_3dim_dataset  *mask_dset  = NULL ; /* mask dataset */
static byte              *mask_vol   = NULL;  /* mask volume */
static int mask_nvox = 0, mask_ngood = 0;     /* number of good voxels in mask volume */

#define INMASK(ijk) (mask_vol==NULL || mask_vol[ijk]!=0)

/* 3D indexes for each point in the mask */

static ind_t *ipmask=NULL , *jpmask=NULL , *kpmask=NULL ;
static int   *ijkmask=NULL ;

/* map from 1D index in volume to points in the mask */

static int *ijk_to_vec=NULL ;

static int   nx ;     /* 3D grid stuff */
static int   ny ;
static int   nz ;
static int   nxy ;
static int   nxyz ;
static int   nxyz1 ;
static float dx ;
static float dy ;
static float dz ;
static int   niter ;  /* number of iterations */

#define PMAX 0.5

static double pthr_init[5] = { 0.0100, 0.0056, 0.0031, 0.0018, 0.0010 } ;
static double athr_init[5] = { 0.05 , 0.04 , 0.03 , 0.02 , 0.01 } ;

static int    npthr = 5 ;
static double *pthr = NULL ;

static float  *zthr_1sid = NULL ;
static float  *zthr_2sid = NULL ;

static int    nathr = 5 ;
static double *athr = NULL ;

static int verb = 1 ;
static int nthr = 1 ;  /* default number of threads */

static int nnlev = 1 ;
static int nndil = 0 ;

#undef DECLARE_ithr
#ifdef USE_OMP
# define DECLARE_ithr const int ithr=omp_get_thread_num()
#else
# define DECLARE_ithr const int ithr=0
#endif

static int minmask = 128 ;   /* 29 Mar 2011 */

static char *prefix = "Xsim.nii" ;

static THD_3dim_dataset **inset = NULL ; /* input datasets */
static int           num_inset  = 0 ;
static int          *nval_inset = NULL ;

#undef  PSMALL
#define PSMALL 1.e-15

static char *cmd_fname = "3dClustSim.cmd" ;

/*----------------------------------------------------------------------------*/
/*! Threshold for upper tail probability of N(0,1) */

double zthresh( double pval )
{
        if( pval <= 0.0 ) pval = PSMALL ;
   else if( pval >= 1.0 ) pval = 1.0 - PSMALL ;
   return qginv(pval) ;
}

/*---------------------------------------------------------------------------*/

static int vsnn=0 ;

static void vstep_reset(void){ vsnn=0; }

static void vstep_print(void)
{
   static char xx[10] = "0123456789" ;
   fprintf(stderr , "%c" , xx[vsnn%10] ) ;
   if( vsnn%10 == 9) fprintf(stderr,".") ;
   vsnn++ ;
}

/*---------------------------------------------------------------------------*/
/* load the input datasets */

int load_insets(void)
{
   int nin=0,qq,nbad=0 ;

   for( qq=0 ; qq < num_inset ; qq++ ){
     nin += nval_inset[qq] ;
     ININFO_message("loading -inset '%s' with %d volumes",
                    DSET_HEADNAME(inset[qq]),DSET_NVALS(inset[qq])) ;
     DSET_load(inset[qq]) ;
     if( !DSET_LOADED(inset[qq]) ){
       ERROR_message("Can't load dataset -inset '%s'",DSET_HEADNAME(inset[qq])) ;
       nbad++ ;
     }
   }
   if( nbad > 0 ) ERROR_exit("Can't continue after load errors") ;

   return nin ;
}

/*---------------------------------------------------------------------------*/
/* Routine to initialize the input options (values are in global variables). */

void get_options( int argc , char **argv )
{
  char * ep;
  int nopt=1 , ii , have_pthr=0;

ENTRY("get_options") ;

  /*----- add to program log -----*/

  pthr = (double *)malloc(sizeof(double)*npthr) ;
  memcpy( pthr , pthr_init , sizeof(double)*npthr ) ;

  athr = (double *)malloc(sizeof(double)*nathr) ;
  memcpy( athr , athr_init , sizeof(double)*nathr ) ;

  while( nopt < argc ){

    /*-----*/

    if( strcmp(argv[nopt],"-NN") == 0 ){
      if( ++nopt >= argc )
        ERROR_exit("You need 1 argument after option '-NN'") ;
      nnlev = (int)strtod(argv[nopt],NULL) ;
      if( nnlev < 1 || nnlev > 3 )
        ERROR_exit("-NN must be 1 or or 3 :(") ;
      nopt++ ; continue ;
    }

    /*-----*/

#if 0
    if( strcmp(argv[nopt],"-dilate") == 0 ){
      if( ++nopt >= argc )
        ERROR_exit("You need 1 argument after option '-dilate'") ;
      nndil = (int)strtod(argv[nopt],NULL) ;
      nopt++ ; continue ;
    }
#endif

    /*-----  -inset iii  -----*/

    if( strcmp(argv[nopt],"-inset") == 0 ){  /* 02 Feb 2016 */
      int ii,nbad=0 ; THD_3dim_dataset *qset ;
      if( num_inset > 0 )
        ERROR_exit("You can't use '-inset' more than once!") ;
      if( ++nopt >= argc )
        ERROR_exit("You need at least 1 argument after option '-inset'") ;
      for( ; nopt < argc && argv[nopt][0] != '-' ; nopt++ ){
        /* ININFO_message("opening -inset '%s'",argv[nopt]) ; */
        qset = THD_open_dataset(argv[nopt]) ;
        if( qset == NULL ){
          ERROR_message("-inset '%s': failure to open dataset",argv[nopt]) ;
          nbad++ ; continue ;
        }
        for( ii=0 ; ii < DSET_NVALS(qset) ; ii++ ){
          if( DSET_BRICK_TYPE(qset,ii) != MRI_float ){
            ERROR_message("-inset '%s': all sub-bricks must be float :-(",argv[nopt]) ;
            nbad++ ; break ;
          }
        }
        if( num_inset > 0 && DSET_NVOX(qset) != DSET_NVOX(inset[0]) ){
          ERROR_message("-inset '%s': grid size doesn't match other datasets",argv[nopt]) ;
          nbad++ ;
        }
        inset      = (THD_3dim_dataset **)realloc( inset     , sizeof(THD_3dim_dataset *)*(num_inset+1)) ;
        nval_inset = (int *)              realloc( nval_inset, sizeof(int)               *(num_inset+1)) ;
        inset[num_inset] = qset ; nval_inset[num_inset] = DSET_NVALS(qset) ; num_inset++ ;
      }
      if( num_inset == 0 ) ERROR_exit("no valid datasets opened after -inset :-(") ;
      if( nbad      >  0 ) ERROR_exit("can't continue after above -inset problems") ;
      continue ;
    }

    /**** -mask mset ****/

    if( strcmp(argv[nopt],"-mask") == 0 ){
      if( mask_dset != NULL ) ERROR_exit("Can't use -mask twice!") ;
      nopt++ ; if( nopt >= argc ) ERROR_exit("need argument after -mask!") ;
      mask_dset = THD_open_dataset(argv[nopt]);
      if( mask_dset == NULL ) ERROR_exit("can't open -mask dataset!") ;
      mask_vol = THD_makemask( mask_dset , 0 , 1.0,0.0 ) ;
      if( mask_vol == NULL ) ERROR_exit("can't use -mask dataset!") ;
      mask_nvox = DSET_NVOX(mask_dset) ;
      DSET_unload(mask_dset) ;
      mask_ngood = THD_countmask( mask_nvox , mask_vol ) ;
      if( mask_ngood < minmask ){
        if( minmask > 2 && mask_ngood > 2 ){
          ERROR_message("-mask has only %d nonzero voxels; minimum allowed is %d.",
                        mask_ngood , minmask ) ;
          ERROR_message("To run this simulation, please read the CAUTION and CAVEAT in -help,") ;
          ERROR_message("and then you can use the '-OKsmallmask' option if you so desire.") ;
          ERROR_exit("Cannot continue -- may we meet under happier circumstances!") ;
        } else if( mask_ngood == 0 ){
          ERROR_exit("-mask has no nonzero voxels -- cannot use this at all :-(") ;
        } else {
          ERROR_exit("-mask has only %d nonzero voxel%s -- cannot use this :-(",
                     mask_ngood , (mask_ngood > 1) ? "s" : "\0" ) ;
        }
      }
      if( verb ) INFO_message("%d voxels in mask (%.2f%% of total)",
                              mask_ngood,100.0*mask_ngood/(double)mask_nvox) ;
      nopt++ ; continue ;
    }

    /*-----  -prefix -----*/

    if( strcmp(argv[nopt],"-prefix") == 0 ){
      nopt++ ; if( nopt >= argc ) ERROR_exit("need argument after -prefix!") ;
      prefix = strdup(argv[nopt]) ;
      if( !THD_filename_ok(prefix) ) ERROR_exit("bad -prefix option!") ;
      nopt++ ; continue ;
    }

    /*----   -quiet   ----*/

    if( strcasecmp(argv[nopt],"-quiet") == 0 ){
      verb = 0 ; nopt++ ; continue ;
    }

    /*----- unknown option -----*/

    ERROR_exit("3dClustSim -- unknown option '%s'",argv[nopt]) ;
  }

  /*------- finalize some simple setup stuff --------*/

#define INSET_PRELOAD

  if( num_inset <= 0 ) ERROR_exit("-inset option is mandatory :(") ;

  nx = DSET_NX(inset[0]) ;
  ny = DSET_NY(inset[0]) ;
  nz = DSET_NZ(inset[0]) ;
  dx = fabsf(DSET_DX(inset[0])) ;
  dy = fabsf(DSET_DY(inset[0])) ;
  dz = fabsf(DSET_DZ(inset[0])) ;
  niter = load_insets() ;
  if( niter < 1000 )
    WARNING_message("-inset has only %d volumes total (= new '-niter' value)",niter) ;
  else if( verb )
    INFO_message("-inset had %d volumes",niter) ;
  if( mask_dset != NULL ){
    if( nx != DSET_NX(mask_dset) ||
        ny != DSET_NY(mask_dset) ||
        nz != DSET_NZ(mask_dset)   )
      ERROR_exit("-mask and -inset don't match in grid dimensions :-(") ;
  }

  nxy = nx*ny ; nxyz = nxy*nz ; nxyz1 = nxyz - nxy ;
  if( nxyz < 256 )
    ERROR_exit("Only %d voxels in simulation?! Need at least 256.",nxyz) ;

  if( mask_ngood == 0 ) mask_ngood = nxyz ;

  /* make a list of the i,j,k coordinates of each point in the mask */

  { int pp,qq , xx,yy,zz ;
    ipmask = (ind_t *)malloc(sizeof(ind_t)*mask_ngood) ;
    jpmask = (ind_t *)malloc(sizeof(ind_t)*mask_ngood) ;
    kpmask = (ind_t *)malloc(sizeof(ind_t)*mask_ngood) ;
    ijkmask= (int *  )malloc(sizeof(int)  *mask_ngood) ;
    for( pp=qq=0 ; qq < nxyz ; qq++ ){
      if( INMASK(qq) ){
        IJK_TO_THREE(qq,xx,yy,zz,nx,nxy) ;
        ipmask[pp] = (ind_t)xx; jpmask[pp] = (ind_t)yy; kpmask[pp] = (ind_t)zz;
        ijkmask[pp] = xx+yy*nx+zz*nxy ;
        pp++ ;
      }
    }
    ijk_to_vec = (int *)malloc(sizeof(int)*nxyz) ;
    for( pp=qq=0 ; qq < nxyz ; qq++ )
      ijk_to_vec[qq] = INMASK(qq) ? pp++ : -1 ;
  }

  /*-- z-score thresholds for the various p-values --*/

  zthr_1sid = (float *)malloc(sizeof(float)*npthr) ;
  zthr_2sid = (float *)malloc(sizeof(float)*npthr) ;
  for( ii=0 ; ii < npthr ; ii++ ){
    zthr_1sid[ii] = (float)zthresh(     pthr[ii] ) ;
    zthr_2sid[ii] = (float)zthresh( 0.5*pthr[ii] ) ;
  }

  EXRETURN ;
}

/*---------------------------------------------------------------------------*/
/* Create the "functional" image, from the inset datasets */

void generate_fim_inset( float *fim , int ival )
{
   if( ival < 0 || ival >= niter ){            /* should not be possible */
     ERROR_message("inset[%d] == out of range!",ival) ;
     memset( fim , 0 , sizeof(float)*nxyz ) ;
   } else {
     float *bar=NULL ; int ii,qq,qval ;
     for( qval=ival,qq=0 ; qq < num_inset ; qq++ ){  /* find which */
       if( qval < nval_inset[qq] ) break ;           /* inset[qq] to use */
       qval -= nval_inset[qq] ;
     }
     if( qq == num_inset ){                    /* should not be possible */
       ERROR_message("inset[%d] == array overflow !!",ival) ;
     } else {
       bar = DSET_ARRAY(inset[qq],qval) ;
       if( bar == NULL ){
#pragma omp critical
         { ININFO_message("loading -inset '%s' with %d volumes",
                           DSET_HEADNAME(inset[qq]),DSET_NVALS(inset[qq])) ;
           DSET_load(inset[qq]) ;
         }
         bar = DSET_ARRAY(inset[qq],qval) ;
       }
     }
     if( bar != NULL ){
       for( ii=0 ; ii < nxyz ; ii++ ) fim[ii] = bar[ii] ;   /* copy data */
       DSET_unload_one(inset[qq],qval) ;
     } else {                                       /* should not happen */
       ERROR_message("inset[%d] == NULL :-(",ival) ;
       memset( fim , 0 , sizeof(float)*nxyz ) ;
     }
   }
}

/*---------------------------------------------------------------------------*/
/* Generate random smoothed masked image, with stdev=1. */

void generate_image( float *fim , int iter )
{
  register int ii ; register float sum ;

  /* Outsource the creation of the random field */

  generate_fim_inset( fim , iter ) ;

  if( mask_vol != NULL ){
    for( ii=0 ; ii < nxyz ; ii++ ) if( !mask_vol[ii] ) fim[ii] = 0.0f ;
  }

  return ;
}

/*----------------------------------------------------------------------------*/
/* Temporary clusters, one for each thread -- allocated in main() */
/* These are created once in main(), and not deleted, to avoid the
   overhead of malloc/free, which tends to degrade OpenMP performance */

static Xcluster **Xctemp_g = NULL ;

/*----------------------------------------------------------------------------*/
/* Add a point to a cluster (if fim is nonzero at this point).
   For use only in the cluster finding functions directly below!
*//*--------------------------------------------------------------------------*/

#define XPUT_point(i,j,k)                                              \
 do{ int pqr = (i)+(j)*nx+(k)*nxy , npt=xcc->npt ;                     \
     if( fim[pqr] != 0.0f ){                                           \
       if( npt == xcc->nall ){                                         \
         xcc->nall += DALL + xcc->nall/2 ;                             \
         xcc->ip = (ind_t *)realloc(xcc->ip,sizeof(ind_t)*xcc->nall) ; \
         xcc->jp = (ind_t *)realloc(xcc->jp,sizeof(ind_t)*xcc->nall) ; \
         xcc->kp = (ind_t *)realloc(xcc->kp,sizeof(ind_t)*xcc->nall) ; \
         xcc->ijk= (int *)  realloc(xcc->ijk,sizeof(int) *xcc->nall) ; \
       }                                                               \
       xcc->ip[npt] = (i); xcc->jp[npt] = (j); xcc->kp[npt] = (k);     \
       xcc->ijk[npt] = pqr ;                                           \
       xcc->npt++ ; xcc->norig++ ;                                     \
       xcc->fom += ADDTO_FOM(fim[pqr]) ;                               \
       fim[pqr] = 0.0f ;                                               \
     } } while(0)

/*----------------------------------------------------------------------------*/
/* Find clusters (NN1 mode), keep the one with the biggest FOM. */

Xcluster * find_fomest_Xcluster_NN1( float *fim , int ithr )
{
   Xcluster *xcc , *xccout=NULL ;
   int ii,jj,kk, icl , ijk , ijk_last ;
   int ip,jp,kp , im,jm,km ;
   float fom_max=0.0f ;

   xcc = Xctemp_g[ithr] ; /* pick the working cluster struct for this thread */

   ijk_last = 0 ;  /* start scanning at the {..wait for it..} start */

   while(1) {
     /* find next nonzero point in fim array */

     for( ijk=ijk_last ; ijk < nxyz ; ijk++ ) if( fim[ijk] != 0.0f ) break ;
     if( ijk == nxyz ) break ;  /* didn't find any! */
     ijk_last = ijk+1 ;         /* start here next time */

     IJK_TO_THREE(ijk, ii,jj,kk , nx,nxy) ;  /* 3D coords of this point */

     /* build a new cluster starting with this 1 point */

     xcc->ip[0] = ii; xcc->jp[0] = jj; xcc->kp[0] = kk; xcc->ijk[0] = ijk;
     xcc->npt   = xcc->norig = 1 ;
     xcc->fom   = ADDTO_FOM(fim[ijk]) ; fim[ijk] = 0.0f ;

     /* loop over points in cluster, checking their neighbors,
        growing the cluster if we find any that belong therein */

     for( icl=0 ; icl < xcc->npt ; icl++ ){
       ii = xcc->ip[icl]; jj = xcc->jp[icl]; kk = xcc->kp[icl];
       im = ii-1        ; jm = jj-1        ; km = kk-1 ;  /* minus 1 indexes */
       ip = ii+1        ; jp = jj+1        ; kp = kk+1 ;  /* plus 1 indexes */

       if( im >= 0 ) XPUT_point(im,jj,kk) ;  /* put this point if fim[] is nonzero */
       if( ip < nx ) XPUT_point(ip,jj,kk) ;
       if( jm >= 0 ) XPUT_point(ii,jm,kk) ;
       if( jp < ny ) XPUT_point(ii,jp,kk) ;
       if( km >= 0 ) XPUT_point(ii,jj,km) ;
       if( kp < nz ) XPUT_point(ii,jj,kp) ;
     } /* since xcc->npt increases if XPUT_point adds the point,
          the loop continues until finally no new neighbors get added */

     /* is this the fom-iest cluster yet? if so, save it */

     if( xcc->fom > fom_max ){
       if( xccout == NULL ) xccout = copy_Xcluster(xcc) ;    /* a new copy */
       else                 copyover_Xcluster(xcc,xccout) ;  /* over-write */
       fom_max = xcc->fom ;                 /* the FOM bar has been raised */
     }
   } /* loop until all nonzero points in fim[] have been used up */

   return xccout ;  /* could be NULL, if fim is all zeros */
}

/*----------------------------------------------------------------------------*/
/* Find clusters (NN2 mode), keep the one with the biggest FOM. */

Xcluster * find_fomest_Xcluster_NN2( float *fim , int ithr )
{
   Xcluster *xcc , *xccout=NULL ;
   int ii,jj,kk, icl , ijk , ijk_last ;
   int ip,jp,kp , im,jm,km ;
   float fom_max=0.0f ;

   xcc = Xctemp_g[ithr] ; /* pick the working cluster struct for this thread */

   ijk_last = 0 ;  /* start scanning at the {..wait for it..} start */

   while(1) {
     /* find next nonzero point in fim array */

     for( ijk=ijk_last ; ijk < nxyz ; ijk++ ) if( fim[ijk] != 0.0f ) break ;
     if( ijk == nxyz ) break ;  /* didn't find any! */
     ijk_last = ijk+1 ;         /* start here next time */

     IJK_TO_THREE(ijk, ii,jj,kk , nx,nxy) ;  /* 3D coords of this point */

     /* build a new cluster starting with this 1 point */

     xcc->ip[0] = ii; xcc->jp[0] = jj; xcc->kp[0] = kk; xcc->ijk[0] = ijk;
     xcc->npt   = xcc->norig = 1 ;
     xcc->fom   = ADDTO_FOM(fim[ijk]) ; fim[ijk] = 0.0f ;

     /* loop over points in cluster, checking their neighbors,
        growing the cluster if we find any that belong therein */

     for( icl=0 ; icl < xcc->npt ; icl++ ){
       ii = xcc->ip[icl]; jj = xcc->jp[icl]; kk = xcc->kp[icl];
       im = ii-1        ; jm = jj-1        ; km = kk-1 ;  /* minus 1 indexes */
       ip = ii+1        ; jp = jj+1        ; kp = kk+1 ;  /* plus 1 indexes */

       if( im >= 0 ){  XPUT_point(im,jj,kk) ;
         if( jm >= 0 ) XPUT_point(im,jm,kk) ;  /* 2NN */
         if( jp < ny ) XPUT_point(im,jp,kk) ;  /* 2NN */
         if( km >= 0 ) XPUT_point(im,jj,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(im,jj,kp) ;  /* 2NN */
       }
       if( ip < nx ){  XPUT_point(ip,jj,kk) ;
         if( jm >= 0 ) XPUT_point(ip,jm,kk) ;  /* 2NN */
         if( jp < ny ) XPUT_point(ip,jp,kk) ;  /* 2NN */
         if( km >= 0 ) XPUT_point(ip,jj,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ip,jj,kp) ;  /* 2NN */
       }
       if( jm >= 0 ){  XPUT_point(ii,jm,kk) ;
         if( km >= 0 ) XPUT_point(ii,jm,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ii,jm,kp) ;  /* 2NN */
       }
       if( jp < ny ){  XPUT_point(ii,jp,kk) ;
         if( km >= 0 ) XPUT_point(ii,jp,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ii,jp,kp) ;  /* 2NN */
       }
       if( km >= 0 )   XPUT_point(ii,jj,km) ;
       if( kp < nz )   XPUT_point(ii,jj,kp) ;
     } /* since xcc->npt increases if XPUT_point adds the point,
          the loop continues until finally no new neighbors get added */

     /* is this the fom-iest cluster yet? if so, save it */

     if( xcc->fom > fom_max ){
       if( xccout == NULL ) xccout = copy_Xcluster(xcc) ;    /* a new copy */
       else                 copyover_Xcluster(xcc,xccout) ;  /* over-write */
       fom_max = xcc->fom ;                 /* the FOM bar has been raised */
     }
   } /* loop until all nonzero points in fim[] have been used up */

   return xccout ;  /* could be NULL, if fim is all zeros */
}

/*----------------------------------------------------------------------------*/
/* Find clusters (NN3 mode), keep the one with the biggest FOM. */

Xcluster * find_fomest_Xcluster_NN3( float *fim , int ithr )
{
   Xcluster *xcc , *xccout=NULL ;
   int ii,jj,kk, icl , ijk , ijk_last ;
   int ip,jp,kp , im,jm,km ;
   float fom_max=0.0f ;

   xcc = Xctemp_g[ithr] ; /* pick the working cluster struct for this thread */

   ijk_last = 0 ;  /* start scanning at the {..wait for it..} start */

   while(1) {
     /* find next nonzero point in fim array */

     for( ijk=ijk_last ; ijk < nxyz ; ijk++ ) if( fim[ijk] != 0.0f ) break ;
     if( ijk == nxyz ) break ;  /* didn't find any! */
     ijk_last = ijk+1 ;         /* start here next time */

     IJK_TO_THREE(ijk, ii,jj,kk , nx,nxy) ;  /* 3D coords of this point */

     /* build a new cluster starting with this 1 point */

     xcc->ip[0] = ii; xcc->jp[0] = jj; xcc->kp[0] = kk; xcc->ijk[0] = ijk;
     xcc->npt   = xcc->norig = 1 ;
     xcc->fom   = ADDTO_FOM(fim[ijk]) ; fim[ijk] = 0.0f ;

     /* loop over points in cluster, checking their neighbors,
        growing the cluster if we find any that belong therein */

     for( icl=0 ; icl < xcc->npt ; icl++ ){
       ii = xcc->ip[icl]; jj = xcc->jp[icl]; kk = xcc->kp[icl];
       im = ii-1        ; jm = jj-1        ; km = kk-1 ;  /* minus 1 indexes */
       ip = ii+1        ; jp = jj+1        ; kp = kk+1 ;  /* plus 1 indexes */

       if( im >= 0 ){  XPUT_point(im,jj,kk) ;
         if( jm >= 0 ) XPUT_point(im,jm,kk) ;  /* 2NN */
         if( jp < ny ) XPUT_point(im,jp,kk) ;  /* 2NN */
         if( km >= 0 ) XPUT_point(im,jj,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(im,jj,kp) ;  /* 2NN */
         if( jm >= 0 && km >= 0 ) XPUT_point(im,jm,km) ;  /* 3NN */
         if( jm >= 0 && kp < nz ) XPUT_point(im,jm,kp) ;  /* 3NN */
         if( jp < ny && km >= 0 ) XPUT_point(im,jp,km) ;  /* 3NN */
         if( jp < ny && kp < nz ) XPUT_point(im,jp,kp) ;  /* 3NN */
       }
       if( ip < nx ){  XPUT_point(ip,jj,kk) ;
         if( jm >= 0 ) XPUT_point(ip,jm,kk) ;  /* 2NN */
         if( jp < ny ) XPUT_point(ip,jp,kk) ;  /* 2NN */
         if( km >= 0 ) XPUT_point(ip,jj,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ip,jj,kp) ;  /* 2NN */
         if( jm >= 0 && km >= 0 ) XPUT_point(ip,jm,km) ;  /* 3NN */
         if( jm >= 0 && kp < nz ) XPUT_point(ip,jm,kp) ;  /* 3NN */
         if( jp < ny && km >= 0 ) XPUT_point(ip,jp,km) ;  /* 3NN */
         if( jp < ny && kp < nz ) XPUT_point(ip,jp,kp) ;  /* 3NN */
       }
       if( jm >= 0 ){  XPUT_point(ii,jm,kk) ;
         if( km >= 0 ) XPUT_point(ii,jm,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ii,jm,kp) ;  /* 2NN */
       }
       if( jp < ny ){  XPUT_point(ii,jp,kk) ;
         if( km >= 0 ) XPUT_point(ii,jp,km) ;  /* 2NN */
         if( kp < nz ) XPUT_point(ii,jp,kp) ;  /* 2NN */
       }
       if( km >= 0 )   XPUT_point(ii,jj,km) ;
       if( kp < nz )   XPUT_point(ii,jj,kp) ;
     } /* since xcc->npt increases if XPUT_point adds the point,
          the loop continues until finally no new neighbors get added */

     /* is this the fom-iest cluster yet? if so, save it */

     if( xcc->fom > fom_max ){
       if( xccout == NULL ) xccout = copy_Xcluster(xcc) ;    /* a new copy */
       else                 copyover_Xcluster(xcc,xccout) ;  /* over-write */
       fom_max = xcc->fom ;                 /* the FOM bar has been raised */
     }
   } /* loop until all nonzero points in fim[] have been used up */

   return xccout ;  /* could be NULL, if fim is all zeros */
}

#undef XPUT_point

/*---------------------------------------------------------------------------*/
/* Global cluster collection:
     Xclust_g[ipthr][iter] = cluster at iteration iter and threshold ipthr
   The basic array is created in main().
*//*-------------------------------------------------------------------------*/

static Xcluster ***Xclust_g ;

/*---------------------------------------------------------------------------*/
/* Get a NN1_1sided cluster at a particular threshold (ipthr),
   in a particular thread (ithr), at a particular iteration (iter),
   and save it into the global cluster collection Xclust_g.
*//*-------------------------------------------------------------------------*/

void gather_clusters_NN1_1sid( int ipthr, float *fim, float *tfim, int ithr,int iter )
{
  register int ii ; register float thr ; Xcluster *xcc ;

  thr = zthr_1sid[ipthr] ;
  for( ii=0 ; ii < nxyz ; ii++ )
    tfim[ii] = (fim[ii] > thr) ? fim[ii] : 0.0f ;

  xcc = find_fomest_Xcluster_NN1(tfim,ithr) ;

  Xclust_g[ipthr][iter] = xcc ;

  return ;
}

/*---------------------------------------------------------------------------*/
/* Get a NN2_1sided cluster at a particular threshold (ipthr),
   in a particular thread (ithr), at a particular iteration (iter),
   and save it into the global cluster collection Xclust_g.
*//*-------------------------------------------------------------------------*/

void gather_clusters_NN2_1sid( int ipthr, float *fim, float *tfim, int ithr,int iter )
{
  register int ii ; register float thr ; Xcluster *xcc ;

  thr = zthr_1sid[ipthr] ;
  for( ii=0 ; ii < nxyz ; ii++ )
    tfim[ii] = (fim[ii] > thr) ? fim[ii] : 0.0f ;

  xcc = find_fomest_Xcluster_NN2(tfim,ithr) ;

  Xclust_g[ipthr][iter] = xcc ;

  return ;
}

/*---------------------------------------------------------------------------*/
/* Get a NN3_1sided cluster at a particular threshold (ipthr),
   in a particular thread (ithr), at a particular iteration (iter),
   and save it into the global cluster collection Xclust_g.
*//*-------------------------------------------------------------------------*/

void gather_clusters_NN3_1sid( int ipthr, float *fim, float *tfim, int ithr,int iter )
{
  register int ii ; register float thr ; Xcluster *xcc ;

  thr = zthr_1sid[ipthr] ;
  for( ii=0 ; ii < nxyz ; ii++ )
    tfim[ii] = (fim[ii] > thr) ? fim[ii] : 0.0f ;

  xcc = find_fomest_Xcluster_NN3(tfim,ithr) ;

  Xclust_g[ipthr][iter] = xcc ;

  return ;
}

/*---------------------------------------------------------------------------*/
/* Dilate a cluster by 1 voxel [don't change FOM] */

#define DILATE_point(i,j,k)                                         \
 do{ int pqr = (i)+(j)*nx+(k)*nxy , npt=xc->npt ;                   \
     if( INMASK(pqr) ){                                             \
       if( npt == xc->nall ){                                       \
         xc->nall += DALL + xc->nall ;                              \
         xc->ip = (ind_t *)realloc(xc->ip,sizeof(ind_t)*xc->nall) ; \
         xc->jp = (ind_t *)realloc(xc->jp,sizeof(ind_t)*xc->nall) ; \
         xc->kp = (ind_t *)realloc(xc->kp,sizeof(ind_t)*xc->nall) ; \
         xc->ijk= (int *)  realloc(xc->ijk,sizeof(int) *xc->nall) ; \
       }                                                            \
       xc->ip[npt] = (i); xc->jp[npt] = (j); xc->kp[npt] = (k);     \
       xc->ijk[npt] = pqr ; xc->npt++ ;                             \
     }                                                              \
 } while(0)

/*---------------------------------------------------------------------------*/
/* per-thread work arrays for dilation;
   again, the highest level arrays are created in main(),
   and the sub-arrays are never free()-ed to avoid OpenMP degradation */

static ind_t **dilg_iq=NULL , **dilg_jq=NULL , **dilg_kq=NULL ;
static int   *ndilg=NULL ;

void dilate_Xcluster( Xcluster *xc , int nnlev , int ithr )
{
   int npt,ntry,ii,jj , nx1=nx-1,ny1=ny-1,nz1=nz-1 ;
   ind_t *iq, *jq, *kq , xx,yy,zz ;

   if( xc == NULL || xc->npt == 0 ) return ;

   npt = xc->npt ;

   /* create the list of candidate points for dilation
      (most will not survive the second step: already in cluster) */

   ntry = npt * (  (nnlev<=1) ? 6 : (nnlev==2) ? 18 : 26 ) ;
   if( ntry > ndilg[ithr] ){
     dilg_iq[ithr] = (ind_t *)realloc(dilg_iq[ithr],sizeof(ind_t)*(ntry+64)) ;
     dilg_jq[ithr] = (ind_t *)realloc(dilg_jq[ithr],sizeof(ind_t)*(ntry+64)) ;
     dilg_kq[ithr] = (ind_t *)realloc(dilg_kq[ithr],sizeof(ind_t)*(ntry+64)) ;
     ndilg  [ithr] = ntry+64 ;
   }
   iq = dilg_iq[ithr] ; jq = dilg_jq[ithr] ; kq = dilg_kq[ithr] ;

   for( jj=ii=0 ; ii < npt ; ii++ ){           /* candidates = neighbors */
     xx = xc->ip[ii] ; yy = xc->jp[ii] ; zz = xc->kp[ii] ; /* current pt */
     /* no dilation from outer edges of 3D grid */
     if( xx==0 || xx==nx1 || yy==0 || yy==ny1 || zz==0 || zz==nz-1 ) continue ;

     iq[jj] = xx+1; jq[jj] = yy;   kq[jj] = zz;   jj++;  /* NN1 dilation */
     iq[jj] = xx-1; jq[jj] = yy;   kq[jj] = zz;   jj++;  /* candidates */
     iq[jj] = xx;   jq[jj] = yy+1; kq[jj] = zz;   jj++;
     iq[jj] = xx;   jq[jj] = yy-1; kq[jj] = zz;   jj++;
     iq[jj] = xx;   jq[jj] = yy;   kq[jj] = zz+1; jj++;
     iq[jj] = xx;   jq[jj] = yy;   kq[jj] = zz-1; jj++;
     if( nnlev >= 2 ){
       iq[jj] = xx+1; jq[jj] = yy+1; kq[jj] = zz  ; jj++;
       iq[jj] = xx-1; jq[jj] = yy+1; kq[jj] = zz  ; jj++;
       iq[jj] = xx+1; jq[jj] = yy-1; kq[jj] = zz  ; jj++;
       iq[jj] = xx-1; jq[jj] = yy-1; kq[jj] = zz  ; jj++;
       iq[jj] = xx;   jq[jj] = yy+1; kq[jj] = zz+1; jj++;
       iq[jj] = xx;   jq[jj] = yy-1; kq[jj] = zz+1; jj++;
       iq[jj] = xx;   jq[jj] = yy+1; kq[jj] = zz-1; jj++;
       iq[jj] = xx;   jq[jj] = yy-1; kq[jj] = zz-1; jj++;
       iq[jj] = xx+1; jq[jj] = yy;   kq[jj] = zz+1; jj++;
       iq[jj] = xx-1; jq[jj] = yy;   kq[jj] = zz+1; jj++;
       iq[jj] = xx+1; jq[jj] = yy;   kq[jj] = zz-1; jj++;
       iq[jj] = xx-1; jq[jj] = yy;   kq[jj] = zz-1; jj++;
       if( nnlev >=3 ){
         iq[jj] = xx+1; jq[jj] = yy+1; kq[jj] = zz+1; jj++;
         iq[jj] = xx-1; jq[jj] = yy+1; kq[jj] = zz+1; jj++;
         iq[jj] = xx+1; jq[jj] = yy-1; kq[jj] = zz+1; jj++;
         iq[jj] = xx-1; jq[jj] = yy-1; kq[jj] = zz+1; jj++;
         iq[jj] = xx+1; jq[jj] = yy+1; kq[jj] = zz-1; jj++;
         iq[jj] = xx-1; jq[jj] = yy+1; kq[jj] = zz-1; jj++;
         iq[jj] = xx+1; jq[jj] = yy-1; kq[jj] = zz-1; jj++;
         iq[jj] = xx-1; jq[jj] = yy-1; kq[jj] = zz-1; jj++;
     }}
   }
   ntry = jj ;

   /* for each new point:
        compare to all other points in the cluster
        if it isn't any of them, add it to the cluster */

   for( jj=0 ; jj < ntry ; jj++ ){
     xx = iq[jj] ; yy = jq[jj] ; zz = kq[jj] ; /* trial new pt */
     for( ii=0 ; ii < xc->npt ; ii++ ){        /* check if already in */
       if( xx == xc->ip[ii] && yy == xc->jp[ii] && zz == xc->kp[ii] ) break ;
     }
     if( ii == xc->npt ) /* did not break == new point */
       DILATE_point(xx,yy,zz) ;  /* add it in, Babee! */
   } /* note: xc->npt will expand as the cluster grows */

   return ;  /* cluster is all dilated now */
}

#undef DILATE_point

/*---------------------------------------------------------------------------*/
/* Vector struct.
   One for each point in the mask, to hold the FOM values found at that point.
*//*-------------------------------------------------------------------------*/

typedef struct {
  ind_t ip,jp,kp ; int ijk ;  /* where is this pt? */
  int npt , nall ;             /* array dimensions */
  float *far ;              /* array of FOM values */
  float val ;                 /* summary FOM value */
} Xvector ;

#define CREATE_Xvector(xv,siz)                         \
 do{ xv = (Xvector *)malloc(sizeof(Xvector)) ;         \
     xv->npt = 0 ; xv->nall = (siz) ;                  \
     xv->far = (float *)malloc(sizeof(float)*(siz)) ;  \
 } while(0)

#define DESTROY_Xvector(xv)                            \
 do{ free(xv->far); free(xv); } while(0)

#define ADDTO_Xvector(xv,val)                                         \
 do{ if( xv->npt == xv->nall ){                                       \
       xv->nall += DALL ;                                             \
       xv->far   = (float *)realloc(xv->far,sizeof(float)*xv->nall) ; \
     }                                                                \
     xv->far[xv->npt++] = (val) ;                                     \
 } while(0)

/* global FOM vector array -- created in main() */

static Xvector **fomvec = NULL ;

/*---------------------------------------------------------------------------*/
/* Process the clusters to FOM vectors for a range of 3D indexes,
   for a given p-value threshold index ipthr.
     This function is intended to be use in multiple threads, operating
     over differing ijkbot..ijktop blocks:
       a given fomvec will not be in thread conflict, since it is linked
       to a given voxel in space (ijk), so there is no potential problem
       when updating that fomvec, or the cluster's ijk entry.
*//*-------------------------------------------------------------------------*/

void process_clusters_to_Xvectors( int ijkbot, int ijktop , int ipthr )
{
   Xcluster **xcar = Xclust_g[ipthr] ;
   Xcluster *xc ;
   int cc , pp,npt , ijk,vin ;

   for( cc=0 ; cc < niter ; cc++ ){                    /* loop over clusters */
     xc = xcar[cc] ; if( xc == NULL ) continue ;
     npt = xc->npt ;
     for( pp=0 ; pp < npt ; pp++ ){          /* loop over pts inside cluster */
       ijk = xc->ijk[pp] ;                            /* index of pt in grid */
       if( ijk >= ijkbot && ijk <= ijktop ){  /* is point inside our region? */
         vin = ijk_to_vec[ijk] ;                           /* find its index */
         if( vin >= 0 ){
           ADDTO_Xvector(fomvec[vin],xc->fom) ; /* add to vector of FOM vals */
         }
       }
     }
   }

   return ;
}

/*---------------------------------------------------------------------------*/
/* get median of the number of counts in fomvec's touched by this cluster */

static int    *ncount_g ;
static float **fcount_g ;

int get_Xcluster_nbcount( Xcluster *xc , int ithr )
{
   int nc,ii ; float *fc ;

   if( xc == NULL || xc->npt == 0 ) return 0 ;
   if( xc->npt == 1 ){
     xc->nbcount = fomvec[ijk_to_vec[xc->ijk[0]]]->npt ;
     return xc->nbcount ;
   }

   if( ncount_g[ithr] < xc->npt ){
     ncount_g[ithr] = xc->npt + DALL ;
     fcount_g[ithr] = (float *)realloc(fcount_g[ithr],
                                       sizeof(float)*ncount_g[ithr]) ;
   }
   nc = ncount_g[ithr] ;
   fc = fcount_g[ithr] ;
   for( ii=0 ; ii < xc->npt ; ii++ )
     fc[ii] = fomvec[ijk_to_vec[xc->ijk[ii]]]->npt ;

   xc->nbcount = (int)qmed_float(xc->npt,fc) ;
   return xc->nbcount ;
}

/*---------------------------------------------------------------------------*/
/* find the intermediate value of x at alphat (t=target),
   given x at alpha0 and alpha1, where alpha is a tail (1-CDF) probability,
   assumed to be of the extreme value form
      alpha(x) = 1 - exp( -exp(-(x-p)/q) )
      log(-log(1-alpha(x))) = -x/q + p/q
   Define a(alpha) = log(-log(1-alpha)), so
      a(x) = -x/q + p/q  or  x(a) = p - q*a
   The alpha values are between 0 and 1; alphat is between alpha0 and alpha1.
*//*-------------------------------------------------------------------------*/

static __inline__ float
  inverse_interp_extreme( float alpha0 , float alpha1 , float alphat ,
                          float x0     , float x1                      )
{
   float a0,a1,at ;

   a0 = logf(-logf(1.0f-alpha0)) ;
   a1 = logf(-logf(1.0f-alpha1)) ;
   at = logf(-logf(1.0f-alphat)) ;
   return (x0 + (x1-x0)/(a1-a0)*(at-a0)) ;  /* linear interp in a-space */
}

/*---------------------------------------------------------------------------*/

int main( int argc , char *argv[] )
{
   int qpthr , ii,xx,yy,zz,ijk , dijk ;
   int ndilstep , ndilated[4] , ndilsum ;
   int count_targ100 , count_targ90, count_targ70 ;
   THD_3dim_dataset *qset=NULL ;
   float *qar=NULL , *gthresh=NULL ;
   char qpr[32] ;

   if( argc < 2 || strcasecmp(argv[1],"-help") == 0 ){
     printf("\n"
       "God only knows what this program does (if anything).\n") ;

     printf("\n"
       " -NN     1 or 2 or 3\n"
       " -inset  dsets\n"
       " -prefix something\n"
       " -mask   something\n"
     ) ;
     exit(0) ;
   }

   get_options(argc,argv) ;

   /*----- get the number of threads -----------------------------------*/

 AFNI_OMP_START ;     /*------------ start parallel section ----------*/
#pragma omp parallel
 {
#ifdef USE_OMP
#pragma omp master
   { nthr = omp_get_num_threads() ; }
#else
   nthr = 1 ;
#endif
 }
 AFNI_OMP_END ;       /*---------- end parallel section ----------*/
   if( nthr > 1 )
     INFO_message("3dClustSimX: Using %d OpenMP threads") ;
   else
     INFO_message("3dClustSimX: Using 1 thread -- this will be slow :-(") ;

   /*--- code to initialize the cluster arrays (one for each simulation) ---*/

   Xclust_g = (Xcluster ***)malloc(sizeof(Xcluster **)*npthr) ;
   for( qpthr=0 ; qpthr < npthr ; qpthr++ )
     Xclust_g[qpthr] = (Xcluster **)malloc(sizeof(Xcluster *)*niter) ;

   /*--- thread-specific temporary clusters ---*/

   Xctemp_g = (Xcluster ** )malloc(sizeof(Xcluster * )*nthr) ;

   /*--- loop over realizations to load up Xclust_g[][] ---*/

INFO_message("start 1-sided clustering with NN=%d",nnlev) ;

 AFNI_OMP_START ;      /*------------ start parallel section ----------*/
#pragma omp parallel
 { DECLARE_ithr ;
   int iter , ipthr ;
   float *fim , *tfim ;

   /* code to initialize thread-specific workspace */

   CREATE_Xcluster(Xctemp_g[ithr],DALL) ;
   fim  = (float *)malloc(sizeof(float)*nxyz) ;
   tfim = (float *)malloc(sizeof(float)*nxyz) ;

#pragma omp for
   for( iter=0; iter < niter ; iter++ ){ /* loop over realizations */
     generate_image( fim , iter ) ;
     for( ipthr=0 ; ipthr < npthr ; ipthr++ ){  /* over thresholds */
       if( nnlev == 1 )
         gather_clusters_NN1_1sid( ipthr , fim , tfim , ithr , iter ) ;
       else if( nnlev == 2 )
         gather_clusters_NN2_1sid( ipthr , fim , tfim , ithr , iter ) ;
       else
         gather_clusters_NN3_1sid( ipthr , fim , tfim , ithr , iter ) ;
     }
   }

#if 1
#pragma omp critical
   { ININFO_message(" free workspace for thread %d",ithr) ;
     free(fim) ; free(tfim) ; DESTROY_Xcluster(Xctemp_g[ithr]) ; }
#endif
 }
 AFNI_OMP_END ;       /*---------- end parallel section ----------*/

#if 1
 free(Xctemp_g) ;
#endif

   /* make sure datasets are unloaded (to frugalize memory) */

   for( ii=0 ; ii < num_inset ; ii++ ) DSET_unload(inset[ii]) ;

   /*--- find the global distributions ---------------------------------*/

   { float *fomg=calloc(sizeof(float),niter); int nfom,jj; Xcluster **xcc;
     float a0,a1,f0,f1,ft ;
     gthresh = (float *)calloc(sizeof(float),npthr) ;
     for( qpthr=0 ; qpthr < npthr ; qpthr++ ){
       xcc = Xclust_g[qpthr] ;
       for( nfom=ii=0 ; ii < niter ; ii++ ){
         if( xcc[ii] != NULL ) fomg[nfom++] = xcc[ii]->fom ;
       }
       if( nfom < 100 ) continue ;  /* should not happen */
       qsort_float_rev( nfom, fomg ) ;
       jj = (int)(0.05f*niter) ;
       a0 = ((float)jj)/((float)niter) ; f0 = fomg[jj] ;
       a1 = a0 + 1.0f/((float)niter) ;   f1 = fomg[jj+1] ;
       ft = gthresh[qpthr] = inverse_interp_extreme( a0,a1,0.05f , f0,f1 ) ;
       INFO_message("5%% FOM for pthr=%.5f is %g (nfom=%d)",pthr[qpthr],ft,nfom) ;
     }
     free(fomg) ;
   }

   /*--- initialize the FOM vector array for each voxel ---*/

   fomvec = (Xvector **)malloc(sizeof(Xvector *)*mask_ngood) ;
   for( ii=0 ; ii < mask_ngood ; ii++ ){
     CREATE_Xvector(fomvec[ii],100) ;
     fomvec[ii]->ip  = ipmask[ii] ;   /* store location of this */
     fomvec[ii]->jp  = jpmask[ii] ;   /* vector in the 3D grid */
     fomvec[ii]->kp  = kpmask[ii] ;
     fomvec[ii]->ijk = ijkmask[ii] ;
   }
   ncount_g = (int *)   malloc(sizeof(int)    *nthr) ;
   fcount_g = (float **)malloc(sizeof(float *)*nthr) ;
   for( ii=0 ; ii < nthr ; ii++ ){
     fcount_g[ii] = (float *)malloc(sizeof(float)*DALL) ;
     ncount_g[ii] = DALL ;
   }

   /*--- dilate the clusters -------------------------------------------*/

   /*--- make thread-specific workspaces ---*/

   ndilg   = (int *   )malloc(sizeof(int    )*nthr) ;
   dilg_iq = (ind_t **)malloc(sizeof(ind_t *)*nthr) ;
   dilg_jq = (ind_t **)malloc(sizeof(ind_t *)*nthr) ;
   dilg_kq = (ind_t **)malloc(sizeof(ind_t *)*nthr) ;
   for( ii=0 ; ii < nthr ; ii++ ){
     ndilg  [ii] = DALL ;
     dilg_iq[ii] = (ind_t *)malloc(sizeof(ind_t)*DALL) ;
     dilg_jq[ii] = (ind_t *)malloc(sizeof(ind_t)*DALL) ;
     dilg_kq[ii] = (ind_t *)malloc(sizeof(ind_t)*DALL) ;
   }

   /*----- loop over p-thresholds -----*/

                      dijk = nxyz / nthr ;
   if( dijk > 2*nxy ) dijk = 2*nxy ;
   if( dijk > 16384 ) dijk = 16384 ;

   count_targ100 = (int)rintf(0.0100f*niter) ;
   count_targ90  = (int)rintf(0.0090f*niter) ;
   count_targ70  = (int)rintf(0.0070f*niter) ;

   for( qpthr=0 ; qpthr < npthr ; qpthr++ ){

#define NDILMAX 7

     INFO_message("dilation at pthr=%.5f",pthr[qpthr]) ;
     for( ndilstep=0 ; ndilstep < NDILMAX ; ndilstep++ ){
       ININFO_message(" step %d",ndilstep+1) ;
       ndilated[0] = ndilated[1] = ndilated[2] = ndilated[3] = 0 ;

 AFNI_OMP_START ;      /*------------ start parallel section ----------*/
#pragma omp parallel
 {  DECLARE_ithr ;
    int iter, idil , ijkbot,ijktop ;

#pragma omp master
    { ININFO_message("  compute FOM vectors") ; }

    /* reset the FOM vectors */

#pragma omp for
    for( iter=0 ; iter < mask_ngood ; iter++ ) fomvec[iter]->npt = 0 ;

    /* load the FOM vectors (parallelized across 3D segments) */

#pragma omp for schedule(dynamic,1)
    for( ijkbot=0 ; ijkbot < nxyz ; ijkbot+=dijk ){
      ijktop = ijkbot + (dijk-1) ;
      process_clusters_to_Xvectors( ijkbot, ijktop , qpthr ) ;
    }

    /* get median FOM counts in each cluster,
       then determine how to dilate
       (parallelized across clusters = simulation iterations) */

#pragma omp master
    { ININFO_message("  dilate clusters") ; }

#pragma omp for schedule(dynamic,200)
    for( iter=0 ; iter < niter ; iter++ ){
      if( Xclust_g[qpthr][iter]          != NULL          &&
          Xclust_g[qpthr][iter]->nbcount <  count_targ100   ){
        idil = get_Xcluster_nbcount( Xclust_g[qpthr][iter] , ithr ) ;
        if( idil < count_targ100 ){
          idil = (idil < count_targ70) ? 3 :(idil < count_targ90) ? 2 : 1 ;
          dilate_Xcluster( Xclust_g[qpthr][iter] , idil , ithr ) ;
#pragma omp atomic
          ndilated[idil]++ ;
        }
      }
    }

 }
 AFNI_OMP_END ;       /*---------- end parallel section ----------*/

       ndilsum = ndilated[1] + ndilated[2] + ndilated[3] ;
       ININFO_message("  dilation counts:  NN1=%d  NN2=%d  NN3=%d  total=%d",
                      ndilated[1] , ndilated[2] , ndilated[3] , ndilsum ) ;

       if( ndilstep < NDILMAX-1 && ndilsum < niter/50 ){
         ININFO_message("- breaking out of dilation") ; break ;
       }
     } /* end of loop over dilation steps */
   }

#if 1
   ININFO_message(" free dilation workspace") ;
   for( ii=0 ; ii < nthr ; ii++ ){
     free(dilg_iq[ii]); free(dilg_jq[ii]); free(dilg_kq[ii]);
   }
   free(ndilg); free(dilg_iq); free(dilg_jq); free(dilg_kq);
#endif

INFO_message("re-loading FOM vectors after final dilations") ;
   for( qpthr=0 ; qpthr < npthr ; qpthr++ ){ /* loop over thresholds */

     ININFO_message(" start pthr=%.5f",pthr[qpthr]) ;

 AFNI_OMP_START ;      /*------------ start parallel section ----------*/
# pragma omp parallel
   { DECLARE_ithr ;
     int ijkbot,ijktop , iv,jj , npt ;
     float a0,a1,f0,f1,ft ;

#pragma omp master
     { ININFO_message("  A: compute FOM vectors") ; }

#pragma omp for schedule(dynamic,1)
     for( ijkbot=0 ; ijkbot < nxyz ; ijkbot+=dijk ){
       ijktop = ijkbot + (dijk-1) ;
       process_clusters_to_Xvectors( ijkbot, ijktop , qpthr ) ;
     }

#pragma omp master
     { ININFO_message("  B: delete Xclusters") ; }

     /* delete all clusters for this qpthr */
#pragma omp master
     { for( jj=0 ; jj < niter ; jj++ ){
         DESTROY_Xcluster(Xclust_g[qpthr][jj]) ;
       }
       free(Xclust_g[qpthr]) ; Xclust_g[qpthr] = NULL ;
     }
#pragma omp barrier

#pragma omp master
     { ININFO_message("  C: find thresholds") ; }

     /* vectors loaded for each pt in space;
        now sort them (biggest first) to determine equitable thresholds */

/* NEXT: The 0.002 fraction here is just for testing.
         Have to evaluate the FAR for various values of TFRAC
          to find the correct threshold volumes.
         (a) run mri_threshold_Xcluster() using each FOM threshold
             volume at a given TFRAC -- initialize TFRAC to value
             that gives median cluster size across voxels equal
             to global cluster size for 5%
         (b) if any pthr return non-NULL, that is 1 false alarm
         (c) add up over simulations
         (d) adjust TFRAC to get to 5% FAR */

#define TFRAC 0.002f

#pragma omp for schedule(dynamic,200)
     for( iv=0 ; iv < mask_ngood ; iv++ ){
       npt = fomvec[iv]->npt ; fomvec[iv]->val = 0.0f ;
       if( npt > 1 ){
         qsort_float_rev( npt , fomvec[iv]->far ) ;
         jj = (int)(TFRAC*niter) ;
         if( jj >= npt ) jj = npt/2 ;
         a0 = ((float)jj)/((float)niter) ; f0 = fomvec[iv]->far[jj] ;
         a1 = a0 + 1.0f/((float)niter) ;   f1 = fomvec[iv]->far[jj+1] ;
         ft = inverse_interp_extreme( a0,a1,TFRAC , f0,f1 ) ;
         if( ft < 0.222f*gthresh[qpthr] ) ft = 0.222f*gthresh[qpthr] ;
         fomvec[iv]->val = ft ;
       }
     }
   }
 AFNI_OMP_END ;       /*---------- end parallel section ----------*/

     /* save datasets of fomvec counts */

     qset = EDIT_empty_copy(inset[0]) ;
     sprintf(qpr,".%d",qpthr) ;
     EDIT_dset_items( qset ,
                        ADN_prefix , modify_afni_prefix(prefix,NULL,qpr) ,
                        ADN_nvals  , 2 ,
                      ADN_none ) ;
     EDIT_substitute_brick( qset , 0 , MRI_float , NULL ) ;
     qar = DSET_ARRAY(qset,0) ;
     for( ii=0 ; ii < mask_ngood ; ii++ ){
       ijk = ijkmask[ii] ;
       qar[ijk] = (float)fomvec[ii]->npt ;
       fomvec[ii]->npt = 0 ;
     }
     EDIT_substitute_brick( qset , 1 , MRI_float , NULL ) ;
     qar = DSET_ARRAY(qset,1) ;
     for( ii=0 ; ii < mask_ngood ; ii++ ){
       ijk = ijkmask[ii] ;
       qar[ijk] = fomvec[ii]->val ;
       fomvec[ii]->val = 0.0f ;
     }

     DSET_write(qset); WROTE_DSET(qset);
     DSET_delete(qset); qset = NULL; qar = NULL;

   } /*----- end of loop over qpthr -----*/

   exit(0) ;

}