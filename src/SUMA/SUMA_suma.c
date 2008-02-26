#define DEBUG_1
#ifdef DEBUG_1
	#define DEBUG_2
	#define DEBUG_3
#endif
   
/* Header FILES */
   
#include "SUMA_suma.h"
#include "../afni.h"

/* CODE */


SUMA_SurfaceViewer *SUMAg_cSV; /*!< Global pointer to current Surface Viewer structure*/
SUMA_SurfaceViewer *SUMAg_SVv; /*!< Global pointer to the vector containing the various Surface Viewer Structures 
                                    SUMAg_SVv contains SUMA_MAX_SURF_VIEWERS structures */
int SUMAg_N_SVv = 0; /*!< Number of SVs realized by X */
SUMA_DO *SUMAg_DOv;	/*!< Global pointer to Displayable Object structure vector*/
int SUMAg_N_DOv = 0; /*!< Number of DOs stored in DOv */
SUMA_CommonFields *SUMAg_CF; /*!< Global pointer to structure containing info common to all viewers */

#ifdef SUMA_DISASTER
/*!
   a function to test debugging 
*/
int * SUMA_disaster(void)
{
   static char FuncName[]={"SUMA_disaster"};
   int *iv1=NULL, *iv2 = NULL, *iv3 = NULL;
   int N_iv1, N_iv2;
   int i;
   
   SUMA_ENTRY;
   N_iv1 = 5;
   N_iv2 = 5;
   iv1 = (int*) SUMA_calloc(N_iv1, sizeof(int));
   iv2 = (int*) SUMA_calloc(N_iv2, sizeof(int));
   
   /* overwrite iv1 */
   iv1[N_iv1] = 3;
   
   /* overwrite iv2 */
   iv2[N_iv2] = 7;
   
   /* free iv1 (that should give a warning)*/
   SUMA_free(iv1); /* without the -trace option, 
                      you'll get a warning of this corruption here */

   /* try to free iv3 although it was not allocated for */
   /* AFNI's functions do not check for this ...*/
   /* SUMA_free(iv3);*/
         
   /* don't free iv2, that should only give a warning when you exit with -trace option turned on */
   
   /* if you use -trace, you'll get a warning at the return for iv2 
   All allocated memory will be checked, at the return, not just iv2*/   
   SUMA_RETURN(iv2); 
}

#endif

void SUMA_usage (SUMA_GENERIC_ARGV_PARSE *ps)
   
  {/*Usage*/
          char *sb = NULL, *sio = NULL;
          
          sb = SUMA_help_basics();
          sio  = SUMA_help_IO_Args(ps);
          printf (
"\nUsage:  \n"
" Mode 0: Just type suma to see some toy surface and play\n"
"         with the interface. Some surfaces are generated\n"
"         using T. Lewiner's MarchingCubes library. \n"
"         Use '.' and ',' keys to cycle through surfaces.\n"
"\n"
" Mode 1: Using a spec file to specify surfaces\n"
"                suma -spec <Spec file> \n"
"                     [-sv <SurfVol>] [-ah AfniHost]\n"
"\n"
"   -spec <Spec file>: File containing surface specification. \n"     
"                      This file is typically generated by \n"     
"                      @SUMA_Make_Spec_FS (for FreeSurfer surfaces) or \n"
"                      @SUMA_Make_Spec_SF (for SureFit surfaces). \n"
"                      The Spec file should be located in the directory \n"
"                      containing the surfaces.\n"
"   [-sv <SurfVol>]: Anatomical volume used in creating the surface \n"     
"                    and registerd to the current experiment's anatomical \n"
"                    volume (using @SUMA_AlignToExperiment). \n"
"                    This parameter is optional, but linking to AFNI is \n"
"                    not possible without it.If you find the need for it \n"
"                    (as some have), you can specify the SurfVol in the \n"
"                    specfile. You can do so by adding the field \n"
"                    SurfaceVolume to each surface in the spec file. \n"
"                    In this manner, you can have different surfaces using\n"
"                    different surface volumes.\n"     
"   [-ah AfniHost]: Name (or IP address) of the computer running AFNI. \n"     
"                     This parameter is optional, the default is localhost.\n"
"                     When both AFNI and SUMA are on the same computer, \n"
"                     communication is through shared memory. \n"
"                     You can turn that off by explicitly setting AfniHost\n"
"                     to 127.0.0.1\n"
"   [-niml]: Start listening for NIML-formatted elements.\n"     
"   [-dev]: Allow access to options that are not well polished for\n"
"           mass consuption.\n"   
"\n"
" Mode 2: Using -t_TYPE or -t* options to specify surfaces on command line.\n"
"         -sv, -ah, -niml and -dev are still applicable here. This mode \n"
"         is meant to simplify the quick viewing of a surface model.\n"
"                suma [-i_TYPE surface] [-t* surface] \n"
"         Surfaces specified on command line are place in a group\n"
"         called 'DefGroup'.\n"
"         If you specify nothing on command line, you will have a random\n"
"         surface created for you. Some of these surfaces are generated\n"
"         using Thomas Lewiner's sample volumes for creating isosurfaces.\n"
"         See suma -sources for a complete reference.\n"
"\n"
"%s"
"\n"
" Modes 1 & 2: You can mix the two modes for loading surfaces but the -sv\n"
"              option may not be properly applied.\n"    
"              If you mix these modes, you will have two groups of\n"
"              surfaces loaded into SUMA. You can switch between them\n"
"              using the 'Switch Group' button in the viewer controller.\n" 
"\n"
"%s"
/*"   [-iodbg] Trun on the In/Out debug info from the start.\n"
"   [-memdbg] Turn on the memory tracing from the start.\n" */    
"   [-visuals] Shows the available glxvisuals and exits.\n"
"   [-version] Shows the current version number.\n"
"   [-environment] Shows a list of all environment variables and \n"
"                  and their default setting.\n"
"                  The output can be used as your .sumarc file.\n" 
"   [-latest_news] Shows the latest news for the current \n"
"                  version of the entire SUMA package.\n"
"   [-all_latest_news] Shows the history of latest news.\n"
"   [-progs] Lists all the programs in the SUMA package.\n"
"   [-sources] Lists code sources used in parts of SUMA.\n"
"\n"
"   For help on interacting with SUMA, press 'ctrl+h' with the mouse \n"
"   pointer inside SUMA's window.\n"     
"   For more help: http://afni.nimh.nih.gov/ssc/ziad/SUMA/SUMA_doc.htm\n"
"\n"     
"   If you can't get help here, please get help somewhere.\n", 
            sio, sb);
          SUMA_free(sb); SUMA_free(sio);
			 SUMA_Version(NULL);
			 printf ("\n" 
                  "\n    Ziad S. Saad SSCC/NIMH/NIH saadz@mail.nih.gov \n\n");
          exit (0);
  }/*Usage*/
     


/*!
    a function to return some surface objects for SUMA to work with 
    
    surfaces are added to SUMAg_DOv so let them be freed there....
*/
SUMA_SurfaceObject **SUMA_GimmeSomeSOs(int *N_SOv) 
{
   static char FuncName[]={"SUMA_GimmeSomeSOs"};
   SUMA_SurfaceObject **SOv=NULL, *SO=NULL;
   SUMA_GENERIC_PROG_OPTIONS_STRUCT *Opt;
   char sid[100];
   int i, N_k, k, *ilist=NULL, nhjs;
   float *vlist=NULL; 
   SUMA_Boolean LocalHead = NOPE;
   
   SUMA_ENTRY;
   
   Opt = (SUMA_GENERIC_PROG_OPTIONS_STRUCT *)
            SUMA_calloc(1,sizeof(SUMA_GENERIC_PROG_OPTIONS_STRUCT));

   N_k = 12; /* Think of this number as the number of states, rather than individual surfaces
               10 from isosurface (actually 9, number 6 is removed), 
               1 from HJS collection
               1 from head collection
            */ 
   vlist = (float*)SUMA_calloc(N_k, sizeof(float));
   srand((unsigned int)time(NULL));
   for (i=0; i<N_k; ++i) {
      vlist[i] = rand();
   }
   ilist = SUMA_z_qsort(vlist, N_k);
   
   /* remove six from ilist, bad surface ... */
   for (i=0; i<N_k; ++i) if (ilist[i] == 6) ilist[i] = ilist[N_k-1];
   N_k = N_k - 1; /* remove last one since it replaced 6 */
   
   nhjs=0;
   *N_SOv = 0; 
   i=-1;
   /*       Sequence below, coupled with the use of rygbr20 colormap was necessary in reproducing a crash 
            Crash was likely cause by uninitialized mcb->_case in MarchingCubes.c               ZSS: Oct 06 
      ilist[0]=1;    ilist[1]=4;    ilist[2]=8;    ilist[3]=2;    ilist[4]=10;
       ilist[5]=5;    ilist[6]=0;    ilist[7]=9;    ilist[8]=3;    ilist[9]=7; */
   for (k=0; k<N_k; ++k) {
      if (LocalHead) fprintf(SUMA_STDERR,"ilist[%d]=%d    ", k, ilist[k]);
      if (ilist[k] <= 9) { /* 0 to 9 is code for MarchingCubesSurfaces */
         Opt->obj_type = ilist[k];
         Opt->obj_type_res = 64;
         Opt->debug =0;
         Opt->in_vol =0;
         Opt->mcdatav= NULL;
         if ((SO = SUMA_MarchingCubesSurface(Opt))) {
            ++*N_SOv; SOv = (SUMA_SurfaceObject **) SUMA_realloc(SOv, (*N_SOv)*sizeof(SUMA_SurfaceObject *));
            SOv[*N_SOv-1]=SO;
            /* assign its Group and State and Side and few other things, must look like surfaces loaded with SUMA_Load_Spec_Surf*/
            SOv[*N_SOv-1]->Group = SUMA_copy_string(SUMA_DEF_TOY_GROUP_NAME); /* change this in sync with string in macro SUMA_BLANK_NEW_SPEC_SURF*/
            sprintf(sid, "%s_%d", SUMA_DEF_STATE_NAME, Opt->obj_type);
            SOv[*N_SOv-1]->State = SUMA_copy_string(sid);
            sprintf(sid, "surf_%d", Opt->obj_type);
            SOv[*N_SOv-1]->Label = SUMA_copy_string(sid);
            SOv[*N_SOv-1]->EmbedDim = 3;
            SOv[*N_SOv-1]->AnatCorrect = YUP;
            /* make this surface friendly for suma */
            if (!SUMA_PrepSO_GeomProp_GL(SOv[*N_SOv-1])) {
               SUMA_S_Err("Failed in SUMA_PrepSO_GeomProp_GL");
               SUMA_RETURN(NULL);
            }
            /* Add this surface to SUMA's displayable objects */
            if (!SUMA_PrepAddmappableSO(SOv[*N_SOv-1], SUMAg_DOv, &(SUMAg_N_DOv), 0, SUMAg_CF->DsetList)) {
               SUMA_S_Err("Failed to add mappable SOs ");
               SUMA_RETURN(NULL);
            }
         }
      } else if (ilist[k] == 10) {  /* 10 is code for HJS */
         /* HJS's turn */
         for (nhjs=0; nhjs < 19; ++nhjs) { 
            ++*N_SOv; SOv = (SUMA_SurfaceObject **) SUMA_realloc(SOv, (*N_SOv)*sizeof(SUMA_SurfaceObject *));
            SOv[*N_SOv-1] = SUMA_HJS_Surface(nhjs);
            /* assign its Group and State and Side and few other things, must look like surfaces loaded with SUMA_Load_Spec_Surf*/
            SOv[*N_SOv-1]->Group = SUMA_copy_string(SUMA_DEF_TOY_GROUP_NAME); /* change this in sync with string in macro SUMA_BLANK_NEW_SPEC_SURF*/
            sprintf(sid, "H.J.S.");
            SOv[*N_SOv-1]->State = SUMA_copy_string(sid);
            sprintf(sid, "H.J.S._%d", nhjs);
            SOv[*N_SOv-1]->Label = SUMA_copy_string(sid);
            SOv[*N_SOv-1]->EmbedDim = 3;
            SOv[*N_SOv-1]->AnatCorrect = YUP;
            /* make this surface friendly for suma */
            if (!SUMA_PrepSO_GeomProp_GL(SOv[*N_SOv-1])) {
               SUMA_S_Err("Failed in SUMA_PrepSO_GeomProp_GL");
               SUMA_RETURN(NULL);
            }
            /* Add this surface to SUMA's displayable objects */
            if (!SUMA_PrepAddmappableSO(SOv[*N_SOv-1], SUMAg_DOv, &(SUMAg_N_DOv), 0, SUMAg_CF->DsetList)) {
               SUMA_S_Err("Failed to add mappable SOs ");
               SUMA_RETURN(NULL);
            }
         }
      } else if (ilist[k] == 11) {  /* 11 is code for head */
         if ((SO = SUMA_head_01_surface())) {
            ++*N_SOv; SOv = (SUMA_SurfaceObject **) SUMA_realloc(SOv, (*N_SOv)*sizeof(SUMA_SurfaceObject *));
            SOv[*N_SOv-1]=SO;
            /* assign its Group and State and Side and few other things, must look like surfaces loaded with SUMA_Load_Spec_Surf*/
            SOv[*N_SOv-1]->Group = SUMA_copy_string(SUMA_DEF_TOY_GROUP_NAME); /* change this in sync with string in macro SUMA_BLANK_NEW_SPEC_SURF*/
            SOv[*N_SOv-1]->State = SUMA_copy_string("head_01");
            SOv[*N_SOv-1]->Label = SUMA_copy_string("La_Tete");
            SOv[*N_SOv-1]->EmbedDim = 3;
            SOv[*N_SOv-1]->AnatCorrect = YUP;
            /* make this surface friendly for suma */
            if (!SUMA_PrepSO_GeomProp_GL(SOv[*N_SOv-1])) {
               SUMA_S_Err("Failed in SUMA_PrepSO_GeomProp_GL");
               SUMA_RETURN(NULL);
            }
            /* Add this surface to SUMA's displayable objects */
            if (!SUMA_PrepAddmappableSO(SOv[*N_SOv-1], SUMAg_DOv, &(SUMAg_N_DOv), 0, SUMAg_CF->DsetList)) {
               SUMA_S_Err("Failed to add mappable SOs ");
               SUMA_RETURN(NULL);
            }
         }
      } else {
         SUMA_S_Errv("Bad ilist number: ilist[%d]=%d\n", k, ilist[k]);
         break;
      }
      if (LocalHead) SUMA_Print_Surface_Object(SOv[*N_SOv-1], stderr);
   }
  
   if (Opt) SUMA_free(Opt);
   if (ilist) SUMA_free(ilist);
   if (vlist) SUMA_free(vlist);
   
   SUMA_RETURN(SOv);
}

/*!\**
File : SUMA.c
\author : Ziad Saad
Date : Thu Dec 27 16:21:01 EST 2001
   
Purpose : 
   
   
   
Input paramters : 
\param   
\param   
   
Usage : 
		SUMA ( )
   
   
Returns : 
\return   
\return   
   
Support : 
\sa   OpenGL prog. Guide 3rd edition
\sa   varray.c from book's sample code
   
Side effects : 
   
   
   
***/
int main (int argc,char *argv[])
{/* Main */
   static char FuncName[]={"suma"}; 
	int kar, i;
	SUMA_SFname *SF_name;
	SUMA_Boolean brk, SurfIn;
	char  *NameParam, *AfniHostName = NULL, *s = NULL;
   char *specfilename[SUMA_MAX_N_GROUPS], *VolParName[SUMA_MAX_N_GROUPS];
   byte InMem[SUMA_MAX_N_GROUPS];
	SUMA_SurfSpecFile *Specp[SUMA_MAX_N_GROUPS];   
	SUMA_Axis *EyeAxis; 	
   SUMA_EngineData *ED= NULL;
   DList *list = NULL;
   DListElmt *Element= NULL;
   int iv15[15], N_iv15, ispec, nspec;
   struct stat stbuf;
   float fff=0.0;
   SUMA_Boolean Start_niml = NOPE, Domemtrace = YUP;
   SUMA_GENERIC_ARGV_PARSE *ps=NULL;
   SUMA_Boolean LocalHead = NOPE;
   
    
   SUMA_STANDALONE_INIT;
   SUMA_mainENTRY;
   
	SUMAg_CF->isGraphical = YUP;

   ps = SUMA_Parse_IO_Args(argc, argv, "-i;-t;");
   #if 0
   if (argc < 2)
       {
          SUMA_usage (ps);
          exit (1);
       }
   #endif
		
   /* initialize Volume Parent and AfniHostName to nothing */
   for (ispec=0; ispec < SUMA_MAX_N_GROUPS; ++ispec) {
      specfilename[ispec] = NULL;
      VolParName[ispec] = NULL;
      Specp[ispec] = NULL;
      InMem[ispec] = 0;
   }
	AfniHostName = NULL; 
	
      
	/* Allocate space for DO structure */
	SUMAg_DOv = SUMA_Alloc_DisplayObject_Struct (SUMA_MAX_DISPLAYABLE_OBJECTS);
	
   /* call the function to parse the other surface mode inputs */
   ispec = 0;
   if (LocalHead) SUMA_Show_IO_args(ps);
   if (ps->i_N_surfnames || ps->t_N_surfnames) {
      SUMA_LH("-i and/or -t surfaces on command line!");
      Specp[ispec] = SUMA_IO_args_2_spec (ps, &nspec); 
      if (Specp[ispec]) ++ispec;
      if (nspec != 1) {
         SUMA_S_Errv("-spec is being parsed separately here, expecting one spec only from SUMA_IO_args_2_spec, got %d\n", nspec);
         exit (1);
      }
      
   }
	/* Work the options */
	kar = 1;
	brk = NOPE;
	SurfIn = NOPE;
   Domemtrace = YUP; 
	while (kar < argc) { /* loop accross command ine options */
		/*fprintf(stdout, "%s verbose: Parsing command line...\n", FuncName);*/
		
      if (strcmp(argv[kar], "-h") == 0 || strcmp(argv[kar], "-help") == 0) {
			SUMA_usage (ps);
          exit (1);
		}
		
      if (strcmp(argv[kar], "-visuals") == 0) {
			 SUMA_ShowAllVisuals ();
          exit (0);
		}
      
      if (strcmp(argv[kar], "-version") == 0) {
			 s = SUMA_New_Additions (0.0, 1);
          fprintf (SUMA_STDOUT,"%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      
      if (strcmp(argv[kar], "-sources") == 0) {
			 s = SUMA_sources_Info();
          fprintf (SUMA_STDOUT,"%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      
      if (strcmp(argv[kar], "-all_latest_news") == 0) {
			 s = SUMA_New_Additions (-1.0, 0);
          fprintf (SUMA_STDOUT,"%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      if (strcmp(argv[kar], "-environment") == 0) {
			 s = SUMA_env_list_help ();
          fprintf (SUMA_STDOUT,  "#SUMA DEFAULT ENVIRONMENT \n"
                                 "# If you do not have a ~/.sumarc\n"
                                 "# you can use: \n"
                                 "# suma -environment > ~/.sumarc \n"
                                 "# to create a new one with defaults.\n"
                                 "***ENVIRONMENT\n"
                                 "%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      
      if (strcmp(argv[kar], "-latest_news") == 0) {
			 s = SUMA_New_Additions (0.0, 0);
          fprintf (SUMA_STDOUT,"%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      
      if (strcmp(argv[kar], "-progs") == 0) {
			 s = SUMA_All_Programs();
          fprintf (SUMA_STDOUT,"%s\n", s); 
          SUMA_free(s); s = NULL;
          exit (0);
		}
      
		if (!brk && (strcmp(argv[kar], "-iodbg") == 0)) {
			fprintf(SUMA_STDERR,"Error %s: Obsolete, use -trace\n", FuncName);
			exit (0);
         /*
         fprintf(SUMA_STDOUT,"Warning %s: SUMA running in in/out debug mode.\n", FuncName);
         SUMA_INOUT_NOTIFY_ON; 
			brk = YUP;
         */
		}
      
      SUMA_SKIP_COMMON_OPTIONS(brk, kar);
      
		#if SUMA_MEMTRACE_FLAG
         if (!brk && (strcmp(argv[kar], "-memdbg") == 0)) {
			   fprintf(SUMA_STDOUT,"Error %s: -memdbg is obsolete, use -trace\n", FuncName);
			   exit (0);
            fprintf(SUMA_STDOUT,"Warning %s: SUMA running in memory trace mode.\n", FuncName);
			   SUMAg_CF->MemTrace = YUP;
            #ifdef USING_MCW_MALLOC
            #endif
			   brk = YUP;
		   }
      #endif
      
      if (!brk && (strcmp(argv[kar], "-dev") == 0)) {
			fprintf(SUMA_STDOUT,"Warning %s: SUMA running in developer mode, some options may malfunction.\n", FuncName);
			SUMAg_CF->Dev = YUP;
			brk = YUP;
		}
		
      if (!brk && (strcmp(argv[kar], "-niml") == 0)) {
			Start_niml = YUP;
			brk = YUP;
		}
      
		if (!brk && (strcmp(argv[kar], "-vp") == 0 || strcmp(argv[kar], "-sa") == 0 || strcmp(argv[kar], "-sv") == 0))
		{
			kar ++;
			if (kar >= argc)  {
		  		fprintf (SUMA_STDERR, "need argument after -vp|-sa|-sv \n");
				exit (1);
			}
			if (ispec < 1) {
            fprintf (SUMA_STDERR, "a -spec option must precede the first -sv option\n");
				exit (1);
         }
         if (!specfilename[ispec-1] && !Specp[ispec-1]) {
            fprintf (SUMA_STDERR, "a -spec option must precede each -sv option\n");
				exit (1);
         }
         VolParName[ispec-1] = argv[kar]; 
			if (LocalHead) {
            fprintf(SUMA_STDOUT, "Found: %s\n", VolParName[ispec]);
         }
         
			brk = YUP;
		}		
		
		if (!brk && strcmp(argv[kar], "-ah") == 0)
		{
			kar ++;
			if (kar >= argc)  {
		  		fprintf (SUMA_STDERR, "need argument after -ah\n");
				exit (1);
			}
			if (strcmp(argv[kar],"localhost") != 0) {
            AfniHostName = argv[kar];
         }else {
           fprintf (SUMA_STDERR, "localhost is the default for -ah\nNo need to specify it.\n");
         }
			/*fprintf(SUMA_STDOUT, "Found: %s\n", AfniHostName);*/

			brk = YUP;
		}	
		if (!brk && strcmp(argv[kar], "-spec") == 0)
		{ 
		   kar ++;
		   if (kar >= argc)  {
		  		fprintf (SUMA_STDERR, "need argument after -spec \n");
				exit (1);
			}
			
         if (ispec >= SUMA_MAX_N_GROUPS) {
            fprintf (SUMA_STDERR, 
                     "Cannot accept more than %d spec files.\n",     
                     SUMA_MAX_N_GROUPS);
            exit(1);
         }
         
			specfilename[ispec] = argv[kar]; 
			if (LocalHead) {
            fprintf(SUMA_STDOUT, "Found: %s\n", specfilename[ispec]);
         }
         ++ispec;
			brk = YUP;
		} 
		

		if (!brk && !ps->arg_checked[kar]) {
			fprintf (SUMA_STDERR,
                  "Error %s: Option %s not understood. Try -help for usage\n", 
                  FuncName, argv[kar]);
			exit (1);
		} else {	
			brk = NOPE;
			kar ++;
		}
		
	}/* loop accross command ine options */
   /* -ah option now checked for in ps */
   if (ps->cs->afni_host_name && !AfniHostName) {
      AfniHostName = SUMA_copy_string(ps->cs->afni_host_name);
   }
   
   /* Make surface loading pacifying */
   SetLoadPacify(1);
   
   /* any Specp to be found ?*/
   
	if (specfilename[0] == NULL && Specp[0] == NULL) {
      SUMA_SurfaceObject **SOv=NULL;
      int N_SOv = 0;
      fprintf (SUMA_STDERR,
               "\n"
               "%s: \n"
               "     No input specified, loading some toy surfaces...\n"
               "     Use '.' and ',' to cycle between them.\n"
               "     See suma -help for assistance.\n"
               "\n", FuncName);
		/* create your own surface and put it in a spec file */
      SOv = SUMA_GimmeSomeSOs(&N_SOv);
      Specp[ispec] = SUMA_SOGroup_2_Spec (SOv, N_SOv);
      SUMA_free(SOv); SOv = NULL;
      InMem[ispec] = 1;
      ++ispec;
	}

	if(!SUMA_Assign_HostName (SUMAg_CF, AfniHostName, -1)) {
		fprintf (SUMA_STDERR, "Error %s: Failed in SUMA_Assign_HostName\n", FuncName);
		exit (1);
	}
   
   #ifdef SUMA_DISASTER
   /* a function to test Memtracing */
   {
      int *jnk;
      jnk = SUMA_disaster();
      SUMA_free(jnk); /* without the -trace, you'll get a warning here if jnk is corrupted */
   }
   #endif
      
	/* create an Eye Axis DO */
	EyeAxis = SUMA_Alloc_Axis ("Eye Axis", AO_type);
	if (EyeAxis == NULL) {
		SUMA_error_message (FuncName,"Error Creating Eye Axis",1);
		exit(1);
	}

	/* Store it into SUMAg_DOv */
	if (!SUMA_AddDO(SUMAg_DOv, &SUMAg_N_DOv, (void *)EyeAxis,  AO_type, SUMA_SCREEN)) {
		SUMA_error_message (FuncName,"Error Adding DO", 1);
		exit(1);
	}
	/*fprintf (SUMA_STDERR, "SUMAg_N_DOv = %d created\n", SUMAg_N_DOv);
   SUMA_Show_DOv(SUMAg_DOv, SUMAg_N_DOv, NULL);*/

	/* Allocate space (and initialize) Surface Viewer Structure */
	SUMAg_SVv = SUMA_Alloc_SurfaceViewer_Struct (SUMA_MAX_SURF_VIEWERS);
   
   /* SUMAg_N_SVv gets updated in SUMA_X_SurfaceViewer_Create
   and reflects not the number of elements in SUMAg_SVv which is
   SUMA_MAX_SURF_VIEWERS, but the number of viewers that were realized
   by X */
   
	/* Check on initialization */
	/*SUMA_Show_SurfaceViewer_Struct (SUMAg_cSV, stdout);*/

	/* Create the Surface Viewer Window */
	if (!SUMA_X_SurfaceViewer_Create ()) {
		fprintf(stderr,"Error in SUMA_X_SurfaceViewer_Create. Exiting\n");
		return 1;
	}
   
	for (i=0; i<ispec; ++i) {
      if (!list) list = SUMA_CreateList();
      ED = SUMA_InitializeEngineListData (SE_Load_Group);
      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_cp, (void *)specfilename[i], 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_Head, NULL ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }
      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_ip, (void *)Specp[i], 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_In, Element ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }
      fff = (float) InMem[i];
      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_f, (void *)&fff, 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_In, Element ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }
      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_vp, (void *)VolParName[i], 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_In, Element ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }

      N_iv15 = SUMA_MAX_SURF_VIEWERS;
      if (N_iv15 > 15) {
         fprintf(SUMA_STDERR,"Error %s: trying to register more than 15 viewers!\n", FuncName);
         exit(1);
      }
      for (kar=0; kar<N_iv15; ++kar) iv15[kar] = kar;
      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_iv15, (void *)iv15, 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_In, Element ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }

      if (!( Element = SUMA_RegisterEngineListCommand (  list, ED, 
                                             SEF_i, (void *)&N_iv15, 
                                             SES_Suma, NULL, NOPE, 
                                             SEI_In, Element ))) {
         fprintf(SUMA_STDERR,"Error %s: Failed to register command\n", FuncName);
         exit (1);
      }
   }
   
   if (!SUMA_Engine (&list)) {
      fprintf(SUMA_STDERR,"Error %s: Failed in SUMA_Engine\n", FuncName);
      exit (1);
   }
   
   /* For some reason, I had to add the glLightfv line below
   to force the lightflipping done in SUMA_SetupSVforDOs to take place
   in the A viewer when first opened. I don't know why that is, especially
   since other controllers would show up lit correctly without this glLightfv line below.
   To make matters worse, the A controller's light0_position is correctly flipped.
   It is just that the shading is done as if the position was never flipped. 
   Actually, without the line below, the first time you hit the F key (to manually flip the light), 
   nothing changes, that's because the light's position is unflipped, which is supposed to 
   show the incorrect lighting. You'll have to hit F again to have the lighting correctly flipped 
   and the shading reflecting it.... ZSS, Aug. 05 04 */
   glLightfv(GL_LIGHT0, GL_POSITION, SUMAg_SVv[0].light0_position); 

   if (Start_niml || AFNI_yesenv("SUMA_START_NIML")) {
      if (!list) list = SUMA_CreateList();
      SUMA_REGISTER_HEAD_COMMAND_NO_DATA(list, SE_StartListening, SES_Suma, NULL);

      if (!SUMA_Engine (&list)) {
         fprintf(SUMA_STDERR, "Error %s: SUMA_Engine call failed.\n", FuncName);
         exit (1);   
      }
   }
   SUMA_FreeGenericArgParse(ps); ps = NULL;
   
	/*Main loop */
	XtAppMainLoop(SUMAg_CF->X->App);

	
	/* Done, clean up time */
	if (ispec) {
      int k=0; 
      for (k=0; k<ispec; ++k) {
         if (!SUMA_FreeSpecFields((Specp[k]))) { SUMA_S_Err("Failed to free spec fields"); } 
         Specp[k] = NULL;
      }
   } ispec = 0;
  
	if (!SUMA_Free_Displayable_Object_Vect (SUMAg_DOv, SUMAg_N_DOv)) SUMA_error_message(FuncName,"DO Cleanup Failed!",1);
	if (!SUMA_Free_SurfaceViewer_Struct_Vect (SUMAg_SVv, SUMA_MAX_SURF_VIEWERS)) SUMA_error_message(FuncName,"SUMAg_SVv Cleanup Failed!",1);
	if (!SUMA_Free_CommonFields(SUMAg_CF)) SUMA_error_message(FuncName,"SUMAg_CF Cleanup Failed!",1);
  SUMA_RETURN(0);             /* ANSI C requires main to return int. */
}/* Main */ 


