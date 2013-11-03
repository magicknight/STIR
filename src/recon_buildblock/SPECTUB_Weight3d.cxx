/*
* Copyright (c) 2013,
* Biomedical Image Group (GIB), Universitat de Barcelona, Barcelona, Spain. All rights reserved.
* This software is distributed WITHOUT ANY WARRANTY; 
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

  \author Carles Falcon
*/


//system libraries
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <math.h>

//user defined libraries

#include "stir/recon_buildblock/SPECTUB_Tools.h"
#include "stir/recon_buildblock/SPECTUB_Weight3d.h"
#include "stir/error.h"
#include <boost/format.hpp>

namespace SPECTUB {

#define EPSILON 1e-12
#define EOS '\0'

#define maxim(a,b) ((a)>=(b)?(a):(b))
#define minim(a,b) ((a)<=(b)?(a):(b))
#define abs(a) ((a)>=0?(a):(-a))
#define SIGN(a) (a<-EPSILON?-1:(a>EPSILON?1:0))
 
#ifndef M_PI
#define M_PI 3.14159265
#endif

#define REF_DIST 5.    //reference distance for fanbeam PSF

using namespace std;
//==========================================================================
//=== wm_calculation =======================================================
//==========================================================================

void wm_calculation( const int kOS,
					const angle_type *const ang, 
					voxel_type vox, 
				        bin_type bin, 
					const volume_type& vol, 
					const proj_type& prj, 
					const float *attmap,
					const bool *msk_3d,
					const bool *msk_2d,
					const int maxszb,
					const discrf_type *const gaussdens,
		     const int *const  NITEMS)
{
	
	float weight;
	float coeff_att = (float) 1.;
	int   jp, nel;
	
	//... variables for geometric component ..............................................
	
	float eff;	
	psf_type psf;          // structure for psf distribution
	
	psf.maxszb = maxszb;
	
	if ( wmh.do_psf_3d ) nel = maxszb * maxszb ;
	else nel = maxszb;
	
	psf.val    = new float [ nel ];    // allocation for PSF values
	psf.ib     = new int   [ nel ];    // allocation for PSF indices
	psf.jb     = new int   [ nel ];    // allocation for PSF indices
	

	//... variables for attenuation component .............................................
		
	attpth_type *attpth = 0; // initialise to avoid compiler warning
	int sizeattpth = 1; // initialise to avoid compiler warning
	
	if ( wmh.do_att ){

		if ( !wmh.do_full_att ) sizeattpth = 1 ;
		else sizeattpth = nel ;
	
		attpth = new attpth_type [ sizeattpth ] ;		
		attpth[ 0 ].maxlng = vol.Ncol + vol.Nrow + vol.Nsli ; // maximum length of an attenuation path

		for (int i = 0 ; i < sizeattpth ; i++ ){
			
			attpth[ i ].dl = new float [ attpth[ 0 ].maxlng ];
		    attpth[ i ].iv = new int   [ attpth[ 0 ].maxlng ];
			attpth[ i ].maxlng = attpth[ 0 ].maxlng;
		}
	}
	
	//... to fill projection indices for STIR format .............................
	
	if ( wm.do_save_STIR ){ 
		
		jp = -1;											// projection index (row index of the weight matrix )
		int j1;
		
		for ( int j = 0 ; j < prj.NangOS ; j++ ){
			
			j1 = wmh.index[ j ];			
			
			for ( int k = 0 ; k < prj.Nsli ; k++ ){
				
				for ( int i = 0 ; i < prj.Nbin ; i++){
					
					jp++;
					wm.na[ jp ] = j1;
					wm.nb[ jp ] = i - (int)prj.Nbind2;
					wm.ns[ jp ] = k;
				}
			}
		}
	}	
	
	//=== LOOP1: IMAGE ROWS =======================================================================
	
	for ( vox.irow = 0 ; vox.irow < vol.Nrow ; vox.irow++ ){
		
                //cout << "weights: " << 100.*(vox.irow+1)/vol.Nrow << "%" << endl;
		
		vox.y = vol.y0 + vox.irow * vol.szcm ;       // y coordinate of the voxel (index 0->Nrow-1: irow)
		
		//=== LOOP2: IMAGE COLUMNS =================================================================
		
		for ( vox.icol = 0 ; vox.icol < vol.Ncol ; vox.icol++ ){
			
			vox.x  = vol.x0 + vox.icol * vol.szcm ;     // x coordinate of the voxel (index 0->Ncol-1: icol)
			vox.ip = vox.irow * vol.Ncol + vox.icol ;	 // in-plane index of the voxel considering the slice as an array
 			
			//... to apply mask .........................................
			
			if ( wmh.do_msk){
				
				if ( !msk_2d[ vox.ip ] ) continue;    // to skip voxel if it is outside the 2d_mask  
			}
			
			//=== LOOP3: ANGLES INTO SUBSETS ========================================================
			
			for( int k = 0 ; k < prj.NangOS ; k++ ){
				
				int ka = wmh.index[ k ];			// angle index of the current projection (considering the whole set of projections)
						
				//... perpendicular distance form voxel to detection plane ...........................
				
				vox.dv2dp = vox.x * ang[ ka ].sin - vox.y * ang[ ka ].cos + ang[ ka ].Rrad ;
				
				if ( vox.dv2dp <= 0. ) continue;	// skipping voxel if it is beyond the detection plane (corner voxels)
				
				//... x coordinate in the rotated frame ..............................................
				
				vox.x1    = vox.x * ang[ ka ].cos + vox.y * ang[ ka ].sin ;		
				
				//... to project voxels onto the detection plane and to calculate other distances .....
				
                voxel_projection( &vox , &eff , prj.lngcmd2 );
				
				//... setting PSF to zero	.........................................	
				
				for ( int i = 0 ; i < nel ; i++ ){
					
					psf.val[ i ] = (float)0.;
					psf.ib[ i ]  = psf.jb[ i ] = 0;
				}	
				
				//... correction for PSF ..............................
				
				if ( !wmh.do_psf  )	fill_psf_no ( &psf, vox, &ang[ ka ], bin.szdx );
				
				else{
					
					if ( wmh.do_psf_3d ) fill_psf_3d ( &psf, vox, gaussdens, bin.szdx, bin.thdx, bin.thcmd2 );
					
					else fill_psf_2d ( &psf, vox, gaussdens, bin.szdx );
				}
				
				//... correction for attenuation .................................................
				
				if ( wmh.do_att ){
					
					vox.z = (float)0. ;
					
					if ( !wmh.do_full_att ){    // simple correction for attenuation
						
						bin.x = ang[ ka ].xbin0 + vox.xd0 * ang[ ka ].cos;   // x coord of the projection of the center of the voxel in the detection line
						bin.y = ang[ ka ].ybin0 + vox.xd0 * ang[ ka ].sin; 		
						bin.z = (float)0. ;
						
						calc_att_path( bin, vox, vol, &attpth[ 0 ]);
					}
					else{                       // full correction for attenuation
						
						for ( int i = 0 ; i < psf.Nib ; i++ ){
							
							bin.x = ang[ ka ].xbin0 + ang[ ka ].incx * ( (float)psf.ib[ i ] + (float)0.5 );
							bin.y = ang[ ka ].ybin0 + ang[ ka ].incy * ( (float)psf.ib[ i ] + (float)0.5 );	
							bin.z = (float)psf.jb[ i ] * vox.thcm ;
							
							calc_att_path( bin, vox, vol, &attpth[ i ]);			
						}
					}
				}
			
				//=== LOOP4: IMAGE SLICES ================================================================
				
				for ( vox.islc = 0 ; vox.islc < vol.Nsli ; vox.islc++ ){
					
					vox.iv = vox.ip + vox.islc * vol.Npix ;   // volume index of the voxel (volume as an array)
					
					if ( wmh.do_msk ){
						if ( !msk_3d[ vox.iv ] ) continue;
					}
					
					if ( wmh.do_att && !wmh.do_full_att ) coeff_att = calc_att( &attpth[ 0 ], attmap , vox.islc );
					
					//... weight matrix values calculation .......................................
					
					for ( int i = 0 ; i < psf.Nib ; i++ ){
						
						if ( psf.ib[ i ] < 0 ) continue;					
						if ( psf.ib[ i ] >= prj.Nbin ) break;
						
						//... to fill image STIR indices ...........................
						
						if ( wm.do_save_STIR ){
							wm.nx[ vox.iv ] = (short int)( vox.icol - (int) floor( vol.Ncold2 ) );  // centered index for STIR format
							wm.ny[ vox.iv ] = (short int)( vox.irow - (int) floor( vol.Nrowd2 ) );  // centered index for STIR format
							wm.nz[ vox.iv ] = (short int)  vox.islc ;                               // non-centered index for STIR format
						}
						
						int ks = (vox.islc + psf.jb[ i ]);
						
						if ( ks < vol.first_sl ) continue;						
						if ( ks >= vol.last_sl ) break;
						
						jp = k * prj.Nbp + ks * prj.Nbin + psf.ib[ i ];		
						
						if ( wmh.do_full_att ) coeff_att = calc_att( &attpth[ i ], attmap, vox.islc );
						
						weight = psf.val[ i ] * eff * coeff_att;
						
						wm.col[ jp ][ wm.ne[ jp ] ] = vox.iv;
						wm.val[ jp ][ wm.ne[ jp ] ] = weight;
						wm.ne[ jp ]++;
						
						if ( wm.ne[ jp ] >= NITEMS[ jp ] ) {
							error_weight3d(45, "" );
						}
					}   
				}                    // end of LOOP4: image slices
			}                        // end of LOOP3: projection angle into subset
		}                            // end of LOOP2: image rows		
	}                                // end of LOOP1: image cols
	delete [] psf.val;
	delete [] psf.ib;
	delete [] psf.jb;
	
	if ( wmh.do_att ){
		for ( int i = 0 ; i < sizeattpth ; i++ ){
			delete [] attpth[ i ].dl;
			delete [] attpth[ i ].iv;
		}
		delete [] attpth;
	}
}


//=============================================================================
//=== wm_size_estimation ====================================================
//=============================================================================

void wm_size_estimation (int kOS,
						 const angle_type * const ang, 
						 voxel_type vox, 
						 bin_type bin, 
						 const volume_type& vol, 
						 const proj_type& prj, 
						 const bool * const msk_3d,
						 const bool *const msk_2d,
						 const int maxszb,
						 const discrf_type * const gaussdens,
						 int *NITEMS)
{

	int   jp, nel;
	float eff;
	
	//... variables for geometric component ..............................................

	psf_type psf;          // structure for psf distribution
	
	psf.maxszb = maxszb;
	
	if ( wmh.do_psf_3d ) nel = maxszb * maxszb ;
	else nel = maxszb;
	
	psf.val    = new float [ nel ];    // allocation for PSF values
	psf.ib     = new int   [ nel ];    // allocation for PSF indices
	psf.jb     = new int   [ nel ];    // allocation for PSF indices
	

	
	//=== LOOP1: IMAGE ROWS =======================================================================
	
	for ( vox.irow = 0 ; vox.irow < vol.Nrow ; vox.irow++ ){
		
		vox.y = vol.y0 + vox.irow * vol.szcm ;       // y coordinate of the voxel (index 0->Nrow-1: irow)		
		
		//=== LOOP2: IMAGE COLUMNS =================================================================
		
		for ( vox.icol = 0 ; vox.icol < vol.Ncol ; vox.icol++ ){
			
			vox.x  = vol.x0 + vox.icol * vol.szcm ;     // x coordinate of the voxel (index 0->Ncol-1: icol)
			vox.ip = vox.irow * vol.Ncol + vox.icol ;	 // in-plane index of the voxel considering the slice as an array
 			
			//... to apply mask .........................................
			
			if ( wmh.do_msk){
				
				if ( !msk_2d[ vox.ip ] ) continue;    // to skip voxel if it is outside the 2d_mask  
			}
			
			//=== LOOP3: ANGLES INTO SUBSETS ========================================================
			
			for( int k = 0 ; k < prj.NangOS ; k++ ){
				
				int ka = wmh.index[ k ];			// angle index of the current projection (considering the whole set of projections)
				
				//... perpendicular distance form voxel to detection plane ...........................
				
				vox.dv2dp = vox.x * ang[ ka ].sin - vox.y * ang[ ka ].cos + ang[ ka ].Rrad ;
						
				if ( vox.dv2dp <= 0. ) continue;	// skipping voxel if it is beyond the detection plane (corner voxels)
				
				//... x coordinate in the rotated frame ..............................................
				
				vox.x1    = vox.x * ang[ ka ].cos + vox.y * ang[ ka ].sin ;		
				
				//... to project voxels onto the detection plane and to calculate other distances .....
				
                voxel_projection( &vox , &eff , prj.lngcmd2 );
				
				//... setting PSF to zero	.........................................	
				
				for ( int i = 0 ; i < nel ; i++ ){
					psf.val[ i ] = (float) 0.;
					psf.ib[ i ] = psf.jb[ i ] = 0;
				}
				
				//... correction for PSF ..............................	
				
				if ( !wmh.do_psf  )	fill_psf_no ( &psf, vox, &ang[ ka ], bin.szdx );
				
				else{
					
					if ( wmh.do_psf_3d ) fill_psf_3d ( &psf, vox, gaussdens, bin.szdx, bin.thdx, bin.thcmd2 );
					
					else fill_psf_2d ( &psf, vox, gaussdens, bin.szdx );
				}
				
				//... weight matrix values calculation .......................................
						
				for ( int i = 0 ; i < psf.Nib ; i++ ){
							
					if ( psf.ib[ i ] < 0 ) continue;			
					if ( psf.ib[ i ] >= prj.Nbin ) break;
					
					//=== LOOP4: IMAGE SLICES ================================================================
					
					for (vox.islc = 0 ; vox.islc < vol.Nsli ; vox.islc++ ){	
						
						int ks = (vox.islc + psf.jb[ i ]);
						
						if ( ks < vol.first_sl ) continue;		
						if ( ks >= vol.last_sl ) break;

						vox.iv = vox.ip + vox.islc * vol.Npix ;   // volume index of the voxel (volume as an array)
						
						if ( wmh.do_msk ){
							if( !msk_3d[ vox.iv ] ) continue;
						}
						
						jp = k * prj.Nbp + ks * prj.Nbin + psf.ib[ i ];		
						
						NITEMS[ jp ]++;						
					}
				}                    
			}                        // end of LOOP3: projection angle into subset
		}                            // end of LOOP2: image rows		
	}                                // end of LOOP1: image cols

	delete [] psf.val;
	delete [] psf.ib;
	delete [] psf.jb;
}	

//==========================================================================
//=== calc_gauss ===========================================================
//==========================================================================

void calc_gauss( discrf_type *gaussdens )
{
	float K0 = (float)0.3989422863; //Normalization factor: 1/sqrt(2*M_PI)
	float x  = 0;
	float g;
	
	gaussdens->val[ gaussdens->lngd2 ] = K0;
	float resd2 = gaussdens->res / (float)2.0;
	
	
	for( int i = 1 ; i <= gaussdens->lngd2 ; i++ ){
		
		x += gaussdens->res;
		g = K0 * exp( - x * x / (float)2.);
		gaussdens->val[ gaussdens->lngd2 + i ]  = gaussdens->val[ gaussdens->lngd2 - i ] = g;
	}
	
	gaussdens->acu[ 0 ] = gaussdens->val[ 0 ] * resd2 ;
	
	for ( int i = 1 ; i < gaussdens->lng ; i++ ){
		gaussdens->acu[ i ] = gaussdens->acu[ i - 1 ] + ( gaussdens->val[ i -1 ] + gaussdens->val[ i ] ) * resd2;
	}
	
	for ( int i = 0 ; i < gaussdens->lng ; i++ ){
		gaussdens->acu[ i ] = (gaussdens->acu[ i ] - gaussdens->acu[ 0 ] ) / gaussdens->acu[ gaussdens->lng - 1 ];
	}
}

//==========================================================================
//=== calc_vxprj =========================================================
//==========================================================================

void calc_vxprj( angle_type *ang )
{
	//... initialization to zero .....................................
	
	for ( int j = 0 ; j < ang->vxprj.lng ; j++ ) ang->vxprj.acu[ j ] = ang->vxprj.val[ j ] = (float)0.;
	
	//... total number of points (at DX resolution) ........................
	
	int Nmax= 2 * ang->N2 ;	
	float resd2 = ang->vxprj.res / (float)2.0;
	
	//... plateau...........................................................
	
	for ( int i = 0 ; i < ang->N1 ; i++ ){
		ang->vxprj.val[ ang->N2 - i - 1 ] = ang->vxprj.val[ ang->N2 + i ] = ang->p;
	}
	
	//... slopes of the trapezoid ..........................................
	
	for ( int i = ang->N1 ; i < ang->N2 ; i++ ){
		ang->vxprj.val[ ang->N2 - i - 1 ] = ang->vxprj.val[ ang->N2 + i ] = maxim (ang->m * ((float)i + (float)0.5) + ang->n, 0);
	}
	
	//... cumulative sum ...................................................
	
	ang->vxprj.acu[ 0 ] = ang->vxprj.val[ 0 ] * resd2 ;
	
	for ( int i = 1 ; i < Nmax ; i++ ){
		ang->vxprj.acu[ i ] = ang->vxprj.acu[ i - 1 ] + ( ang->vxprj.val[ i -1 ] + ang->vxprj.val[ i ] ) * resd2;
	}
	
	//... forcing distribution function to have area 1 ......................
	
	for ( int i = 0 ; i < Nmax ; i++ ){
		ang->vxprj.acu[ i ] /= ang->vxprj.acu[ Nmax - 1 ];
	}
}


//==========================================================================
//=== voxel_projection =====================================================
//==========================================================================

void voxel_projection ( voxel_type *vox, float * eff, float lngcmd2)
{
	
	if ( wmh.COL.do_fb ){				// fan_beam
		
		//... angle between voxel-focal line and center-focal line and distance from voxel projection to detection line relevant points........
		
		vox->costhe = cos( atan ( vox->x1 / ( wmh.COL.F - vox->dv2dp ) ) );        
		vox->xdc    = wmh.COL.F * vox->x1 / ( wmh.COL.F - vox->dv2dp );  // distance to the center of the detection line
		vox->xd0    = vox->xdc + lngcmd2 ;                          // distance to the begin of the detection line
		
		//... efficiency correction ...............................................................
		
		if ( wmh.do_psf ) *eff = vox->costhe * vox->costhe * ( wmh.COL.F - REF_DIST ) / ( wmh.COL.F - vox->dv2dp );
		else *eff = (float) 1.;
		
	}
	else{								// parallel
		
		//... distance from projected voxel (center) to the begin of the detection line ............
		
		vox->xd0 = vox->x1 + lngcmd2;
		
		*eff = (float) 1. ;
	}
}

//==========================================================================
//=== fill_psf_no ==========================================================
//==========================================================================

void fill_psf_no( psf_type *psf, const voxel_type& vox, angle_type const *const ang , float szdx )
{
	psf->sgmcm   = vox.szcm;

	if ( wmh.COL.do_fb){
		if ( fabs( vox.x1 ) > EPSILON ) psf->sgmcm  *= vox.xdc / vox.x1;   // fb expanded projection of the voxel
	}
	
	psf->di		 = min( (int) floor( szdx / psf->sgmcm ), ang->vxprj.lng -1 ) ; 
	psf->lngcm	 = ( fabs ( ang->sin ) + fabs( ang->cos ) ) * psf->sgmcm ; 
	
	psf->lngcmd2 = psf->lngcm / (float)2.;
	psf->efres   = ang->vxprj.res * psf->sgmcm;  // to resize discretization resolution once applied sgmcm
	
	calc_psf_bin( vox.xd0, wmh.prj.szcm, &ang->vxprj, psf );	
								   
}

//==========================================================================
//=== fill_psf_2d ==========================================================
//==========================================================================

void fill_psf_2d( psf_type *psf, const voxel_type& vox, discrf_type const* const gaussdens, float szdx )
{
	
	psf->sgmcm   = calc_sigma_h( vox, wmh.COL );	
	
	psf->di      = min ( (int) floor( szdx / psf->sgmcm ), gaussdens->lng -1) ;
	psf->lngcmd2 = psf->sgmcm * wmh.maxsigm ;
	psf->lngcm   = psf->lngcmd2 * (float)2.;
	
	psf->efres   = gaussdens->res * psf->sgmcm ;
	
	calc_psf_bin( vox.xd0, wmh.prj.szcm, gaussdens, psf );		
}

//==========================================================================
//=== fill_psf_3d ==========================================================
//==========================================================================

void fill_psf_3d ( psf_type *psf, const voxel_type& vox, discrf_type const * const gaussdens, float szdx, float thdx, float thcmd2 )
{
	psf_type psf_h;        // structure with horizontal psf distribution
	psf_type psf_v;        // structure with vertical psf distribution	
	
	//... horizontal component ...........................
	
	psf_h.maxszb  = psf->maxszb;
	psf_h.val     = new float [ psf->maxszb ];  // allocation for PSF values (horizontal component of PSF)
	psf_h.ib      = new int   [ psf->maxszb ];  // allocation for PSF indices (horizontal component of PSF)

	psf_h.sgmcm   = calc_sigma_h( vox, wmh.COL);
	psf_h.lngcmd2 = psf_h.sgmcm * wmh.maxsigm ;
	psf_h.lngcm   = psf_h.lngcmd2 * (float)2.;
	psf_h.di      = min( (int) floor( szdx / psf_h.sgmcm ), gaussdens->lng - 1 ) ;
	psf_h.efres   = gaussdens->res * psf_h.sgmcm ;
	
	//... setting PSF to zero	.........................................	
	
	for ( int i = 0 ; i < psf->maxszb ; i++ ){
		psf_h.val[ i ] = (float)0.;
		psf_h.ib[ i ]  = 0;
	}
	
	//... calculation of the horizontal component of psf ...................
	
	calc_psf_bin( vox.xd0, wmh.prj.szcm, gaussdens, &psf_h );	

	//... vertical component ..............................
	
	psf_v.maxszb  = psf->maxszb;
	psf_v.val     = new float[ psf->maxszb ];   // allocation for PSF values (vertical component of PSF)
	psf_v.ib      = new int  [ psf->maxszb ];   // allocation for PSF indices (vertical component of PSF)
	
	psf_v.sgmcm   = calc_sigma_v( vox, wmh.COL);
	psf_v.lngcmd2 = psf_v.sgmcm * wmh.maxsigm;
	psf_v.lngcm   = psf_v.lngcmd2 * (float)2.;
	psf_v.di      = min( (int) floor( thdx / psf_v.sgmcm ), gaussdens->lng - 1 ) ;
	psf_v.efres   = gaussdens->res * psf_v.sgmcm ;
	
	//... setting PSF to zero	.........................................	
	
	for ( int i = 0 ; i < psf->maxszb ; i++ ){
		psf_v.val[ i ] = (float)0.;
		psf_v.ib[ i ]  = 0;
	}
	
	//... calculation of the vertical component of psf ....................
	
	calc_psf_bin( thcmd2, wmh.prj.thcm, gaussdens, &psf_v );	
	
	//... mixing and setting PSF area to 1 (to correct for tail truncation of Gaussian function) .....
	
	float w;
	float area = 0;
	int ip = 0;
	float Nib_hp2 = (float) (psf_h.Nib * psf_h.Nib / 4);
	float Nib_vp2 = (float) (psf_v.Nib * psf_v.Nib / 4);
	
	
	for ( int i = 0 ; i < psf_h.Nib ; i++ ){
		
		float b = ( (float) psf_h.ib[ 0 ] + (float)psf_h.ib[ psf_h.Nib -1 ] )/ (float)2. ;
		float a = ( (float)psf_h.ib[ i ] - b ) * ( (float)psf_h.ib[ i ] - b ) / Nib_hp2;
		
		for ( int j = 0 ; j < psf_v.Nib ; j++ ){
			
			if ( ( a + (float)(psf_v.ib [ j ] * psf_v.ib [ j ]) / Nib_vp2 ) > (float)1. ) continue;
				
			w = psf_h.val[ i ] * psf_v.val[ j ];
			
			if ( w < wmh.min_w ) continue;
			
			psf->val[ ip ] = w;
			psf->ib [ ip ] = psf_h.ib [ i ];
			psf->jb [ ip ] = psf_v.ib [ j ];
			ip++;
			
			area += w;
		}
	}
	psf->Nib = ip;
	
	for( int i = 0 ; i < ip ; i++ ) psf->val[ i ] /= area ; 
	
	//... to delete allocated memory .................
	
	delete [] psf_h.val ;
	delete [] psf_h.ib ;
	delete [] psf_v.val ;
	delete [] psf_v.ib ;
}

//==========================================================================
//=== calc_psf_bin =========================================================
//==========================================================================

void calc_psf_bin (float center_psf,
				   float binszcm,
				   discrf_type const* const vxprj,
				   psf_type *psf)
{
	float weight, preval;

	//... position (in cm and bin index) of the first extrem of the vxprj on the detection line........
	
	float beg_psf = center_psf - psf->lngcmd2;				  // position of the begin of the psf in the detection line (cm)
	int   jm      = (int) floor( beg_psf / binszcm );         // first index in detection line interacting with psf (it can be negative)
	float r_nextb = (float)( jm + 1 ) * binszcm ;             // position of the next change of bin (cm)
	int   i1	  = min( (int) floor( ( r_nextb - beg_psf ) / psf->efres) , vxprj->lng -1 ) ;   // index in vxprj distribution in which happens the change of bin			
	int   Ncb     = ( vxprj->lng - i1 - 1 ) / psf->di ;       // number of complete bins covered by PSF
	
	//... first weigth calculation ...............................................................................
	
	int   ip = 0;											// counter for the number of surviving weights
	float area = (float)0. ;
	
	weight = vxprj->acu[ i1 ] - vxprj->acu[ 0 ];
	
	if ( weight >= wmh.min_w ){
		psf->val[ ip ] = weight;	
		psf->ib[ ip ]  = jm; 
		area += weight;
		ip++;
	}
	
	//... weight for the complete bins ...................................................................
	
	preval = vxprj->acu[ i1 ];
	
	for ( int i = 0 ; i < Ncb ; i++ ){
		jm++;
		i1    += psf->di;
		weight = vxprj->acu[ i1 ] - preval ;
		preval = vxprj->acu[ i1 ];
		
		if ( weight >= wmh.min_w){
			psf->val[ ip ] = weight;
			psf->ib[ ip ]  = jm;
			area += weight;
			ip++;
		}		
	}
	
	//... weight for the last bin ...................................................................

	weight  =  (float)1. - preval ;
	jm++;
	
	if ( weight >= wmh.min_w){
		psf->val[ ip ] = weight;
		psf->ib[ ip ]  = jm;
		area += weight;
		ip++;
	}	
	
	if ( ip >= psf->maxszb ) 
		error_weight3d( 47, "" ); 
	for (int i = 0 ; i < ip ; i++) psf->val[ i ] /= area;
	psf->Nib = ip;

}

//=============================================================================
//=== cal_att_path ============================================================
//=============================================================================

void calc_att_path(const bin_type& bin, const voxel_type& vox, const volume_type& vol, attpth_type *attpth )
{
	float dx, dy, dz;
	float dlast_x, dlast_y, dlast_z, dlast;
	float next_x, next_y, next_z;
	int   cas;
	
	//... to initializate attpth to zero..............................
	
	for (int i = 0 ; i < attpth->maxlng	; i++ ){
		attpth->dl [ i ] = (float) 0.;
		attpth->iv [ i ] = 0;
	}
	
	//... vector from voxel to bin and the sign of its components ....
	
	float ux = bin.x - vox.x;        // first component of voxel_to_bin vector
	float uy = bin.y - vox.y;        // second component of voxel_to_bin vector
	float uz = bin.z - vox.z;        // third component of voxel_to_bin vector
	
	int signx = SIGN(ux);               // sign of ux
	int signy = SIGN(uy);               // sign of uy
	int signz = SIGN(uz);               // sign of uz
	
	//... corresponding unary vector ...................................
	
	float dpb = sqrt(ux*ux + uy*uy + uz*uz); // distance from voxel_to_bin (modulus of [ux,uy,uz])
	ux /= dpb;                               // unit vector ux
	uy /= dpb;					             // unit vector uy
	uz /= dpb;                               // unit vector uz
	
	//... next and last distance to the attenuation map grip ..................
	
	if ( signx < 0 ){
		next_x  = ( (float) vox.icol - (float) 0.5 ) * vox.szcm + vol.x0;
		dlast_x = ( -vol.Xcmd2 - vox.x ) / ( ux + EPSILON ) ;
	}
	else{
		next_x  = ( (float) vox.icol + (float) 0.5 ) * vox.szcm + vol.x0;
		dlast_x = ( vol.Xcmd2 - vox.x ) / ( ux + EPSILON ) ;
	}
	
	if ( signy < 0 ){
		next_y  = ( (float) vox.irow - (float) 0.5 ) * vox.szcm + vol.y0;
		dlast_y = ( -vol.Ycmd2 - vox.y ) / ( uy + EPSILON ) ;
	}
	else{
		next_y  = ( (float) vox.irow + (float) 0.5 ) * vox.szcm + vol.y0;
		dlast_y = ( vol.Ycmd2 - vox.y ) / ( uy + EPSILON ) ;
	}
	
	if ( signz < 0 ){
		next_z  = ( - (float) 0.5 ) * vox.thcm ;
		dlast_z = ( -vol.Zcmd2 - vox.z ) / ( uz + EPSILON ) ;
	}
	else{
		next_z  = ( (float) 0.5 ) * vox.thcm ;
		dlast_z = ( vol.Zcmd2 - vox.z ) / ( uz + EPSILON ) ;
	}
	
	dlast = minim ( minim ( dlast_x, dlast_y ) , minim ( dlast_z , dpb ) );
	
	// ... distance to next planes avoiding high values for parallel or almost parallel lines ...
	
	dx = ( next_x - vox.x ) / ( ux + EPSILON ) ;
	dy = ( next_y - vox.y ) / ( uy + EPSILON ) ;
	dz = ( next_z - vox.z ) / ( uz + EPSILON ) ;
	
	//... variables initialization .....................................
	
	float dant  = (float)0. ;   // previous distance (distance from voxel to the last change of voxel in the attenuation map)
    int ni      = 0 ;           // number of voxels in the attenuation path
	int iv      = vox.ip ;      // voxel index on the attenuation map  
	
	//... loop while attenuation ray is inside the attenuation map
	
	for(;;){
		
		cas = comp_dist( dx, dy, dz, dlast );
		
		if ( cas == 0 ){
			
			attpth->lng = ni;
			return;
		}
		
		else{

			if ( ni >= attpth->maxlng ) error_weight3d(49, "");
			
			attpth->iv[ ni ] = iv ;			
			
			switch(cas){
				case 1:			
					attpth->dl[ ni ] = ( dx - dant ) ;
					dant    = dx ;
					iv     += signx ;
					next_x += vox.szcm * signx ;
					dx      = ( next_x - vox.x ) / ( ux + EPSILON ) ;
					break;
				case 2:
					attpth->dl[ ni ] = ( dy - dant ) ;
					dant    = dy ;
					iv     += signy * vol.Ncol ;
					next_y += vox.szcm * signy ;
					dy      = ( next_y - vox.y ) / ( uy + EPSILON ) ;
					break;
				case 3:
					attpth->dl[ ni ] = ( dz - dant ) ;
					dant    = dz;
					iv     += signz * vol.Npix ;
					next_z += vox.thcm * signz ;
					dz      = ( next_z - vox.z ) / ( uz + EPSILON ) ;
					break;
				default:
					error_weight3d (40, "");
			}	
			ni++;
		}
	}		
}

//=============================================================================
//=== comp_dist ===============================================================
//=============================================================================

int comp_dist( float dx,
			   float dy,
			   float dz,
			   float dlast)
{
	int cas;
	
	if ( dx < dy){
		if ( dx< dz) {
			if ( dx > dlast ) cas = 0;   // case 0: end of the iteration
			else cas = 1;                // case 1: minimum value = dx. Next index change in attenuation map is in x direction
		}
		else {
			if ( dz > dlast ) cas = 0;   // case 0: end of the iteration
			else cas = 3;                // case 3: minimum value = dz. Next index change in attenuation map is in z direction
		}
	}
	else{
		if ( dy < dz) {
			if ( dy > dlast ) cas = 0;   // case 0: end of the iteration
			else cas = 2;                // case 2: minimum value = dy. Next index change in attenuation map is in y direction   
		}
		else {
			if ( dz > dlast ) cas = 0;   // case 0: end of the iteration
			else cas = 3;                // case 3: minimum value = dz. Next index change in attenuation map is in z direction		}
		}
	}
	return( cas );
}

//=============================================================================
//=== cal_att =================================================================
//=============================================================================

float calc_att( const attpth_type *const attpth, const float *const attmap , int nsli ){
	
	float att_coef = (float)0.;
	int iv;
	
	for ( int i = 0 ; i < attpth->lng ; i++ ){
		
		iv = attpth->iv[ i ] + wmh.vol.Npix * nsli ;
		
		if ( iv	< 0 || iv >= wmh.vol.Nvox ) break;
		
		att_coef += attpth->dl[ i ] * attmap[ iv ];
	}
	
	att_coef = exp( -att_coef );
	return( att_coef );
}

//==========================================================================
//=== error_weight3d =======================================================
//==========================================================================

void error_weight3d ( int nerr, const string& text )
{
#if 0
	switch(nerr){
		case 13: printf( "\n\nError weight3d: wm.NbOS and/or wm.Nvox are negative"); break;
		case 21: printf( "\n\nError weight3d: undefined collimator. Collimator %s not found\n",text.c_str() ); break;
		case 30: printf( "\n\nError weight3d: can not open \n%s for reading\n", text.c_str() ); break;
		case 31: printf( "\n\nError weight3d: can not open \n%s for writing\n", text.c_str() ); break;
		case 40: printf( "\n\nError weight3d: wrong codification in comp_dist function");break;
		case 45: printf( "\n\nError weight3d: Realloc needed for WM\n"); break;
		case 47: printf( "\n\nError weight3d: psf length greater than maxszb in calc_psf_bin\n"); break;
		case 49: printf( "\n\nError weight3d: attpth larger than allocated\n"); break;
		case 50: printf( "\n\nError weight3d: No header stored in %s \n",text.c_str() ); break;
		default: printf( "\n\nError weight3d: unknown error number on error_weight3d()"); 
	}
	
	exit(0);
#else
        using stir::error;
	switch(nerr){
		case 13: error( "\n\nError weight3d: wm.NbOS and/or wm.Nvox are negative"); break;
		case 21: printf( "\n\nError weight3d: undefined collimator. Collimator %s not found\n",text.c_str() ); break;
		case 30: printf( "\n\nError weight3d: can not open \n%s for reading\n", text.c_str() ); break;
		case 31: printf( "\n\nError weight3d: can not open \n%s for writing\n", text.c_str() ); break;
		case 40: error( "\n\nError weight3d: wrong codification in comp_dist function");break;
		case 45: error( "\n\nError weight3d: Realloc needed for WM\n"); break;
		case 47: error( "\n\nError weight3d: psf length greater than maxszb in calc_psf_bin\n"); break;
		case 49: error( "\n\nError weight3d: attpth larger than allocated\n"); break;
		case 50: printf( "\n\nError weight3d: No header stored in %s \n",text.c_str() ); break;
		default: error( "\n\nError weight3d: unknown error number on error_weight3d()"); 
	}
	
	exit(0);
#endif
}    



} // namespace SPECTUB
