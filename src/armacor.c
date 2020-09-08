/******************************************************************************/
/** This file is for computing the restricted ARMA(p,1) correlations for
    the time series noise models used in AFNI. It is meant to be #include-d
    into the file that actually uses these functions.

    What is 'restricted' about these models?
    1) The MA part is limited to a white noise process added to a 'pure' AR
       process.
    2) The AR process is restricted to have 1 real root and 1 (p=3) or 2 (p=5)
       complex roots:
       - the real root models straightforward exponential decay of correlation
       - the complex roots model oscillatory decay of correlation
    These restrictions are here because I feel these cover the useful cases
    for the types of data AFNI deals with, and then the programs don't have
    to waste time/memory trying to optimize over non-useful parameter ranges.

    RWCox - 01 Jul 2020 - https://rb.gy/9t84mb
*//****************************************************************************/

/*--- Changes made Aug 2020 to avoid use of malloc library in these funcs ---*/

#undef ARMACOR_DEBUG

/*----------------------------------------------------------------------------*/
/* Return the vector of correlations for an ARMA(1,1)model.
   [Modified Aug 2020 to compute inplace rather than create new vector]
*//*--------------------------------------------------------------------------*/

void arma11_correlations_inplace( double a, double b, double ccut, doublevec *corvec )
{
   double lam , cnew ;
   int kk , ncor , ncmax ;

   if( corvec == NULL || corvec->ar == NULL ) return  ;  /* bad */

   corvec->ar[0] = 1.0 ;                      /* diagonal entry */
   corvec->kk    = 1 ;                    /* 1 + max lag so far */

   if( fabs(a) < 0.01 ) return ;                  /* triviality */

        if( a >  0.95 ) a =  0.95 ;                   /* limits */
   else if( a < -0.95 ) a = -0.95 ;

        if( b >  0.95 ) b =  0.95 ;
   else if( b < -0.95 ) b = -0.95 ;

   if( ccut <= 0.0 ) ccut = 0.00001 ;

   ncmax = corvec->nar - 1  ;
   if( ncmax < 1 ) return ;                /* should not happen */

   lam = ((b+a)*(1.0+a*b)/(1.0+2.0*a*b+b*b)) ;
   corvec->ar[1] = lam ;                /* correlation at lag 1 */
   corvec->kk    = 2 ;
   if( fabs(lam) < ccut || fabs(a) < ccut ){  /* if b+a==0 or a==0 */
#ifdef ARMACOR_DEBUG
     INFO_message("ARMA11: a=%g  b=%g  lam=%g  ncor=%d [cut short]" , a , b , lam , 1 ) ;
#endif
     return ;
   }

   if( fabs(lam) >= 1.0 ){            /* also should not happen */
     ERROR_message("arma11_correlations: bad params a=%g b=%g lam=%g",a,b,lam) ;
   }

   for( ncor=1,kk=2 ; kk < ncmax ; kk++ ){
     cnew = corvec->ar[kk] = a * corvec->ar[kk-1] ; ncor++ ;
     if( fabs(cnew) < ccut ) break ;
   }

   /* store number of output values actually saved into corvec */
   /* (equals max lag plus 1) */

#ifdef ARMACOR_DEBUG
   INFO_message("ARMA11: a=%g  b=%g  lam=%g  ncor=%d" , a , b , lam , ncor ) ;
   for( kk=0 ; kk <= ncor ; kk++ )
     fprintf(stderr," %g",corvec->ar[kk]) ;
   fprintf(stderr,"\n") ;
#endif

   corvec->kk = ncor+1 ;
   return ;
}

/*-----------------------------------------------------------------*/
/*---- version of above that creates the new correlation vector ---*/
/*-----------------------------------------------------------------*/

doublevec * arma11_correlations( double a, double b, double ccut, int ncmax )
{
   doublevec *corvec ; int ncor ;

   if( ncmax < 4 ) ncmax = 6666 ;       /* max lag allowed */
   MAKE_doublevec( corvec , ncmax+1 ) ;

   arma11_correlations_inplace( a, b, ccut, corvec ) ;

   ncor = corvec->kk ; /* max index used in corvec->ar = max lag + 1 */

   if( ncor > 0 ){
     RESIZE_doublevec( corvec , ncor+1 ) ;
   } else {
     KILL_doublevec( corvec ) ;  /* set it back to NULL */
   }

   return corvec ;
}

/*============================================================================*/
#ifdef ALLOW_ARMA31
/*----------------------------------------------------------------------------*/
/* Return the vector of correlations for an AR(3) model plus additive white
   noise [=restricted ARMA(3,1)], given the AR(5) generating polynomial as
     phi(z) = (z-a) * (z-r1*exp(+i*t1)) * (z-r1*exp(-i*t1))
   which corresponds to the standard polynomial representation
     phi(z) = z^3 - p1*z^2 - p2*z - p3
   which corresponds to the AR(3) recurrence
     x[n]   = p1*x[n-1] + p2*x[n-2] + p3*x[n-3] + w[n]
   where
     a     = pure exponential decay component
     r1,t1 = decaying oscillation; 0 < r1 < 1;   0 < t1 < PI
             t1 = 2 * PI * TR * f1 [TR = time step; f1 = frequency]
     w[n]  = stationary white noise process driving the linear recurrence
   The first formulation of the model, instead of using the polynomial
   coefficients as in the second formulation above, is simpler to understand
   in terms of the correlation matrix.

   The vrt parameter is the ratio vrt = s^2 / ( s^2 + a^2 )
   where s^2 = variance of AR(3) process
         a^2 = variance of additive white noise process
   and clearly 0 <= vrt <= 1. For a pure AR(3) model, vrt=1.
   Note that this additive white noise has nothing to do with the w[n]
   noise in the AR recurrence! ("vrt" == "variance ratio")

   A maximum of ncmax correlations are computed (ncmax < 4 ==> no limit).
   Correlations are computed until three in a row are below ccut
   (default when ccut <= 0 ==> 0.00001).
*//*--------------------------------------------------------------------------*/

void arma31_correlations_inplace( double a, double r1, double t1,
                                  double vrt, double ccut, doublevec *corvec )
{
   double p1,p2,p3 , cnew , g1,g2 , c1 ;
   int kk , nzz , ncor , ncmax ;

   if( corvec == NULL || corvec->ar == NULL ) return  ;  /* bad */

   corvec->ar[0] = 1.0 ;                      /* diagonal entry */
   corvec->kk    = 1 ;                    /* 1 + max lag so far */

   /* check inputs */

   if( a < 0.0 || r1 < 0.0 ) return ;               /* bad user */

   if( vrt <= 0.01 ||                        /* no AR(3) noise? */
       (a <= 0.01 && r1 <= 0.01) ) return ;

   if( a   > 0.95 ) a   = 0.95 ;  /* limits */
   if( r1  > 0.95 ) r1  = 0.95 ;
   if( vrt > 1.00 ) vrt = 1.00  ;
   if( t1  > PI   ) t1  = PI   ; else if( t1 < 0.0 ) t1 = 0.0 ;

   if( ccut <= 0.0 ) ccut = 0.00001 ;

   ncmax = corvec->nar - 1  ;
   if( ncmax < 1 ) return ;                /* should not happen */

   ccut = ccut / vrt ; if( ccut > 0.01 ) ccut = 0.01 ;

   /* compute polynomial coefficients from input parameters,
      by expansion of
        phi(z) = (z-a) * (z-r1*exp(+i*t1)) * (z-r1*exp(-i*t1))
      to the standard form
        phi(z) = z^3 - p1*z^2 - p2*z - p3                      */

   c1 = cos(t1) ;
   p1 = a + 2.0*r1*c1 ;
   p2 = -2.0*a*r1*c1 - r1*r1 ;  /* Note: r1==0 => p2 = p3 = 0 */
   p3 = a*r1*r1 ;               /*       which is AR(1) case */

   /* solve equations for first two correlations g1 and g2:
        [    1-p2   -p3  ] [ g1 ] = [ p1 ]
        [ -(p3+p1)   1   ] [ g2 ] = [ p2 ]                 */

   cnew = (1.0-p2) - p3*(p1+p3) ;           /* matrix determinant */

   if( fabs(cnew) < 0.001 ){                /* error, should not happen */
     ERROR_message("bad AR(3) setup:\n"
                   "  a  = %g  r1 = %g  t1 = %g\n"
                   "  p1 = %g  p2 = %g  p3 = %g\n"
                   " det = %g"   , a,r1,t1 , p1,p2,p3 , cnew ) ;
     return ;
   }

   /* the correlations */

   g1 = (         p1 + p3      *p2 ) / cnew ;
   g2 = ( (p1+p3)*p1 + (1.0-p2)*p2 ) / cnew ;

   if( fabs(g1) >= 1.0 || fabs(g2) >= 1.0 ){ /* error, should not happen */
     WARNING_message("bad AR(3) setup:\n"
                     "  a  = %g  r1 = %g  t1 = %g\n"
                     "  p1 = %g  p2 = %g  p3 = %g\n"
                     " det = %g\n"
                     "  g1 = %g  g2 = %g" ,
                     a,r1,t1 , p1,p2,p3 , cnew , g1,g2 ) ;
   }

#if 1
{ double alp = ( g1 - r1*c1 ) / ( a - r1*c1 ) ;
  INFO_message("a=%g r=%g t=%g  g1=%g g2=%g alp=%g p1=%g p2=%g p3=%g",
               a,r1,t1 , g1,g2,alp , p1,p2,p3 ) ;
}
#endif

   /* store them into the output vector */

   corvec->ar[0] = 1.0 ;      /* correlation at lag 0 */
   corvec->ar[1] = g1 ;       /* lag 1 */
   corvec->ar[2] = g2 ;       /* lag 2 */
   corvec->kk    = 3 ;        /* number of correlations saved so far */

   /* compute further correlations recursively -- 3 terms == AR(3) */

   for( nzz=0,ncor=2,kk=3 ; kk < ncmax ; kk++ ){

     /* next correlation, at lag kk */

     cnew = p1*corvec->ar[kk-1] + p2*corvec->ar[kk-2] + p3*corvec->ar[kk-3] ;

     /* and save new value */

     corvec->ar[kk] = cnew ; ncor++ ;  /* ncor = lag index just saved */

     /* check to see if we've shrunk as far as needed */

     if( fabs(cnew) < ccut ){
       nzz++ ;                /* how many undersized in a row? */
       if( nzz > 2 ) break ;  /* 3 strikes and you are OUT! */
     } else {
       nzz = 0 ;              /* not too small ==> reset counter */
     }

   } /* end of recurrence loop */

   /* rescale lagged correlations to allow for additive white noise;
      note that we do NOT scale down the lag=0 correlation of 1.0   */

   if( vrt < 1.0 ){
     for( kk=1 ; kk <= ncor ; kk++ ) corvec->ar[kk] *= vrt ;
   }

   /* store number of output values actually saved into corvec */
   /* (equals max lag plus 1) */

   corvec->kk = ncor + 1 ;
   return ;
}

/*-----------------------------------------------------------------*/
/*---- version of above that creates the new correlation vector ---*/
/*-----------------------------------------------------------------*/

doublevec * arma31_correlations( double a , double r1 , double t1 ,
                                 double vrt , double ccut , int ncmax )
{
   doublevec *corvec ; int ncor ;

   if( ncmax < 4 ) ncmax = 6666 ;       /* max lag allowed */
   MAKE_doublevec( corvec , ncmax+1 ) ;

   arma31_correlations_inplace( a, r1, t1, vrt, ccut, corvec ) ;

   ncor = corvec->kk ; /* max index used in corvec->ar = max lag + 1 */

   if( ncor > 0 ){
     RESIZE_doublevec( corvec , ncor+1 ) ;
   } else {
     KILL_doublevec( corvec ) ;  /* set it back to NULL */
   }

   return corvec ;
}
#endif /* ALLOW_ARMA31 */
/*============================================================================*/

/*============================================================================*/
#ifdef ALLOW_ARMA51  /* Not Ready For Prime Time */

/*----------------------------------------------------------------------------*/
/*-- Check for legality as a correlation function, via FFT.
     Return value is scale down factor for correlations, if needed.
*//*--------------------------------------------------------------------------*/

static double vrt_factor( int ncor , doublevec *corvec )
{
   double vrtfac = 1.0 ;
   int nfft , nf2 , icmin , kk ; complex *xc ; float xcmin ;

   if( ncor < 1 || corvec == NULL ) return vrtfac ;

   /* get FFT length to use */

   nfft = csfft_nextup_one35( 4*ncor+31 ) ;
   nf2  = nfft/2 ;
   xc   = (complex *)calloc( sizeof(complex) , nfft ) ;

   /* load correlations (+reflections) into FFT array (float not double) */

   xc[0].r = 1.0 ; xc[0].i = 0.0 ;
   for( kk=1 ; kk <= ncor ; kk++ ){
     xc[kk].r = corvec->ar[kk] ; xc[kk].i = 0.0f ;
     xc[nfft-kk] = xc[kk] ; /* reflection from nfft downwards */
   }

   csfft_cox( -1 , nfft , xc ) ;  /* FFT */

   /* find smallest value in FFT; for an acceptable
      autocorrelation function, they should all be positive */

   xcmin = xc[0].r ; icmin = 0 ;
   for( kk=1 ; kk < nf2 ; kk++ ){
     if( xc[kk].r < xcmin ){ xcmin = xc[kk].r ; icmin = kk ; }
   }
   free(xc) ;  /* no longer needed by this copy of the universe */

   /* if xcmin is negative, must scale vrt down to avoid Choleski failure */
   /* the vrtfac value below is computed from the following idea:
        x  = unaltered correlation values
        xc = FFT of x
        y  = altered correlation values (scaled down by factor f < 1):
               y[0] = x[0] = 1  ;  y[k] = f * x[k] for k > 0
        yc = FFT of y
           = FFT( f * x ) + (1-f) * FFT( [1,0,0,...] )
           = f * xc + (1-f)
        Min value of yc = ycmin = f * xcmin + (1-f) -- recall xcmin < 0
        We want                  ycmin > 0
             or  -f * ( 1 - xcmin) + 1 > 0
             or                      1 > f * ( 1 - xcmin )
             or    ( 1 / ( 1 - xcmin ) > f
        So the largest allowable value of 'f' is 1/(1-xcmin) < 1,
        which is basically the formula below (with a little fudge factor) */

   if( xcmin <= 0.0f )                /* note xcmin <= 0 so denom is >= 1 */
     vrtfac = 0.99 / ( 1.0 - (double)xcmin ) ;

   if( vrtfac < 1.0 )
     INFO_message("AR() min FFT_%d[%d] = %g => f = %g",nfft,icmin,xcmin,vrtfac);

   return vrtfac ;  /* 1.0 ==> all is OK; < 1.0 ==> shrinkage needed */
}

/*----------------------------------------------------------------------------*/
/* Compute the first 4 gammas (correlations) from
   the AR(5) polynomial coefficients, using a linear system
     phi(z) = z^5 - p1*z^4 - p2*z^3 - p3*z^2 - p4*z - p5
     x[n]   = p1*x[n-1] + p2*x[n-2] + p3*x[n-3] + p4*x[n-4] + p5*x[n-5] + w[n]
*//*--------------------------------------------------------------------------*/

double_quad arma51_gam1234( double p1, double p2, double p3, double p4, double p5 )
{
   dmat44 amat , imat ;
   double_quad g1234 ;

   /* the matrix connecting the pj coefficients to the gammas */

   LOAD_DMAT44( amat ,
                 1.0-p2  ,     -p3  , -p4 , -p5 ,
                -(p1+p3) ,  1.0-p4  , -p5 , 0.0 ,
                -(p2+p4) , -(p1+p5) , 1.0 , 0.0 ,
                -(p3+p5) ,     -p2  , -p1 , 1.0  ) ;

   /* invert the matrix */

#ifdef ARMACOR_DEBUG
   ININFO_message("arma51_gam1234: p1 = %g p2 = %g p3 = %g p4 = %g p5 = %g",p1,p2,p3,p4,p5) ;
   DUMP_DMAT44("matrix",amat) ;
   ININFO_message(" determ = %g",generic_dmat44_determinant(amat)) ;
#endif

   imat = generic_dmat44_inverse( amat ) ;

   /* apply inverse to the pj coefficents themselves to get the gammas */

   DMAT44_VEC( imat , p1,p2,p3,p4 ,
               g1234.a , g1234.b , g1234.c , g1234.d ) ;

#ifdef ARMACOR_DEBUG
   DUMP_DMAT44("inverse",imat) ;
   ININFO_message("  g1 = %g g2 = %g g3 = %g g4 = %g" ,
                  g1234.a , g1234.b , g1234.c , g1234.d ) ;
#endif

   return g1234 ;
}

/*------------------------------------------------------------------------------*/
/* Return the vector of correlations for an AR(5) model plus additive white
   noise, given the AR(5) generating polynomial as
     phi(z) = (z-a) * (z-r1*exp(+i*t1)) * (z-r2*exp(+i*t2))
                    * (z-r1*exp(-i*t1)) * (z-r2*exp(-i*t2))
   which corresponds to the standard polynomial representation
     phi(z) = z^5 - p1*z^4 - p2*z^3 - p3*z^2 - p4*z - p5
   which corresponds to the AR(5) autoregression
     x[n]   = p1*x[n-1] + p2*x[n-2] + p3*x[n-3] + p4*x[n-4] + p5*x[n-5] + w[n]
   where
     a     = pure exponential decay component
     r1,t1 = first decaying oscillation 0 < t1 < PI
     r2,t2 = second decaying oscillation
   This formulation of the model, instead of using the polynomial coefficients,
   is simpler to understand in terms of the correlation matrix.

   The vrt parameter is the ratio vrt = s^2 / ( s^2 + a^2 )
   where s^2 = variance of AR(5) process
         a^2 = variance of additive white noise process
   and clearly 0 <= vrt <= 1. For a pure AR(5) model, vrt=1.
   Note that this additive white noise has nothing to do with the w[n]
   noise in the AR(5) recurrence! ("vrt" == "variance ratio")

   A maximum of ncmax correlations are computed (ncmax < 4 ==> no limit).
   Correlations are computed until three in a row are below ccut
   (ccut <= 0 ==> 0.00001).

   NOTE WELL: For some combinations of the (a,r1,t1,r2,t2) parameters,
              the function produced by the AR(5) recurrence is NOT a
              valid (positive definite) autocorrelation function.
              In such a case, a scale factor 'f' < 1 is computed to scale
              down vrt for such parameter combinations. Thus, if you
              input vrt=0.8 and then the program decides f=0.8, you will
              actually get a correlation function with vrt=0.64 - that is,
              the white noise 'floor' will be amplified. [RWC - 15 Jul 2020]
*//*----------------------------------------------------------------------------*/

doublevec * arma51_correlations( double a , double r1 , double t1 ,
                                            double r2 , double t2 ,
                                 double vrt , double ccut , int ncmax )
{
   double_quad g1234 ;
   double p1,p2,p3,p4,p5 , cnew , c1,s1,c2,s2 , vrtfac=1.0 ;
   int kk , nzz , ncor ;
   doublevec *corvec=NULL ;

   if( a < 0.0 || r1 < 0.0 || r2 < 0.0 ) return NULL ; /* bad user */

   if( vrt <= 0.01 ||                           /* no AR(5) noise? */
       (a <= 0.01 && r1 <= 0.01 && r2 <= 0.01 ) ){
     MAKE_doublevec( corvec , 1 ) ;
     corvec->ar[0] = 1.0 ;
     return corvec ;
   }

   if( a   > 0.95 ) a   = 0.95 ;  /* limits */
   if( r1  > 0.95 ) r1  = 0.95 ;
   if( r2  > 0.95 ) r2  = 0.95 ;
   if( vrt > 1.0  ) vrt = 1.0  ;
   if( t1  > PI   ) t1  = PI   ; else if( t1 < 0.0 ) t1 = 0.0 ;
   if( t2  > PI   ) t2  = PI   ; else if( t2 < 0.0 ) t2 = 0.0 ;

   if( ccut  <= 0.0 ) ccut  = 0.00001 ;
   if( ncmax <  4   ) ncmax = 6666 ;

   ccut = ccut / vrt ; if( ccut > 0.05 ) ccut = 0.01 ;

   /* compute polynomial coefficients from input parameters,
      by expansion of the product
        phi(z) = (z-a) * (z-r1*exp(+i*t1)) * (z-r2*exp(+i*t2))
                       * (z-r1*exp(-i*t1)) * (z-r2*exp(-i*t2))
      into the form
        phi(z) = z^5 - p1*z^4 - p2*z^3 - p3*z^2 - p4*z - p5  */

   c1 = cos(t1) ; s1 = sin(t1) ;
   c2 = cos(t2) ; s2 = sin(t2) ;

   p1 =  2.0*r1*c1       + 2.0*r2*c2           + a ;
   p2 = -4.0*r1*r2*c1*c2 - 2.0*a*(r1*c1+r2*c2) - r1*r1 - r2*r2 ;
   p3 = a * ( r1*r1+r2*r2 + 4.0*r1*r2*c1*c2 )  + 2.0*r1*r2*(r2*c1+r1*c2) ;
   p4 = -2.0*a*r1*r2*(r2*c1+r1*c2)             - r1*r1*r2*r2 ;
   p5 = a * r1*r1 * r2*r2 ;

   /* compute first 4 gamma coefficients (correlations)
      from the linear equation connecting p1..p5 to g1..g4 */

   g1234 = arma51_gam1234( p1, p2, p3, p4, p5 ) ;

   if( fabs(g1234.a) >= 1.0 ||
       fabs(g1234.b) >= 1.0 ||
       fabs(g1234.c) >= 1.0 ||
       fabs(g1234.d) >= 1.0   ){  /* error, should not happen */

     WARNING_message("bad AR(5) setup:\n"
                     "  a  = %g  r1 = %g  t1 = %g  r2 = %g  t2 = %g\n"
                     "  p1 = %g  p2 = %g  p3 = %g  p4 = %g  p5 = %g\n"
                     "  g1 = %g  g2 = %g  g3 = %g  g4 = %g" ,
                     a,r1,t1,r2,t2 , p1,p2,p3,p4,p5 ,
                     g1234.a , g1234.b , g1234.c , g1234.d ) ;
   }

   /* store them into the output vector */

   MAKE_doublevec( corvec , ncmax+1 ) ;

   corvec->ar[0] = 1.0 ;      /* correlation at lag 0 */
   corvec->ar[1] = g1234.a ;  /* lag 1 */
   corvec->ar[2] = g1234.b ;  /* lag 2 */
   corvec->ar[3] = g1234.c ;  /* I'll let you guess this one */
   corvec->ar[4] = g1234.d ;  /* lag 4 */

   /* compute further correlations recursively */

   for( nzz=0,ncor=4,kk=5 ; kk < ncmax ; kk++ ){

     /* next correlation, at lag kk */

     cnew =  p1 * corvec->ar[kk-1] + p2 * corvec->ar[kk-2]
           + p3 * corvec->ar[kk-3] + p4 * corvec->ar[kk-4]
           + p5 * corvec->ar[kk-5] ;

     /* and save new value */

     corvec->ar[kk] = cnew ; ncor++ ;  /* ncor = last index saved */

     /* check to see if we've shrunk as far as needed */

     if( fabs(cnew) < ccut ){
       nzz++ ;                /* how many undersized in a row? */
       if( nzz > 2 ) break ;  /* 3 strikes and you are OUT! */
     } else {
       nzz = 0 ;              /* not too small ==> reset counter */
     }

   } /* end of recurrence loop */

   /*-- Check for legality as a correlation function, via FFT --*/
   /*-- If not legal, have to scale vrt down! [15 Jul 2020]   --*/

   vrtfac = vrt_factor( ncor , corvec ) ;
   vrt   *= vrtfac ;

   /* rescale lagged correlations to allow for additive white noise;
      note that we do NOT scale down the lag=0 correlation of 1.0   */

   if( vrt < 1.0 ){
     for( kk=1 ; kk <= ncor ; kk++ ) corvec->ar[kk] *= vrt ;
   }

   /* shrink the output vector to fit what was actually stored */

   RESIZE_doublevec( corvec , ncor+1 ) ;  /* have 0..ncor = ncor+1 of them */

   return corvec ;
}
#endif /* ALLOW_ARMA51 */
/*============================================================================*/
