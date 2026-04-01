#include "lenstool.h"

#include <FL/Fl_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_PNG_Image.H>

#include <fl_imgtk.h>
#include "fl_png.h"
#include "fl_grptk.h"

#include <cmath>
#include <queue>
#include <algorithm>

/*******************************************************************************
   (C)2017 MEDIT, Raph.K.
   ==========================================================================
   Lens verifying tools.
   Version 2,
*******************************************************************************/

////////////////////////////////////////////////////////////////////////////////

//#define DEBUG_HISTO
//#define DEBUG_IMG_TRACE
//#define DEBUG_GETCONTRAST

#define THRESHOLD_HISTOGRAM				26			/// 256 * 0.1
#define THRESHOLD_HISTOGRAM_CUTOFF		50
#define THRESHOLD_PASS_LIMIT			0.85f

////////////////////////////////////////////////////////////////////////////////

typedef struct _relatedvectorf
{
	float x;
	float y;
}relatedvectorf;

const relatedvectorf RegionWH[] = {
	{ 0.07f, 0.07f },
	{ 0.f, 0.f }
};

////////////////////////////////////////////////////////////////////////////////

using namespace std;

namespace LensTooltk{

static Fl_RGB_Image* convert2rgb24( Fl_RGB_Image* srcimg )
{
	if (srcimg != nullptr)
	{
		if ( srcimg->d() >= 3 )
		{
			return (Fl_RGB_Image*)srcimg->copy();
		}
		else
		if ( srcimg->d() == 2 )
		{
			return nullptr;
		}	

		unsigned img_w = srcimg->w();
		unsigned img_h = srcimg->h();
		unsigned img_sz = img_w * img_h;

		const uchar* srcbuff = (const uchar*)srcimg->data()[ 0 ];
		uchar* dstbuff = new uchar[ img_sz * 3 ];
		if (dstbuff != nullptr)
		{
			for (unsigned cnt = 0; cnt < img_sz; cnt++)
			{
				dstbuff[ cnt * 3 + 0 ] = srcbuff[ cnt ];
				dstbuff[ cnt * 3 + 1 ] = srcbuff[ cnt ];
				dstbuff[ cnt * 3 + 2 ] = srcbuff[ cnt ];
			}

			Fl_RGB_Image* retImg = new Fl_RGB_Image( dstbuff, img_w, img_h, 3 );

			if (retImg != nullptr)
			{
				return retImg;
			}

			delete[] dstbuff;
		}
	}

	return nullptr;
}

static void drawrectonimage( Fl_RGB_Image* refimg, LensTool::RectCoord &rcoord, unsigned col )
{
	if ( refimg != nullptr )
	{
		fl_imgtk::vecpoint vcoord[4];

		// (0)+------------------+(1)
		//    |                  |
		// (3)+------------------+(2)
		vcoord[ 0 ].x = rcoord.x;
		vcoord[ 0 ].y = rcoord.y;
		vcoord[ 1 ].x = rcoord.x + rcoord.w;
		vcoord[ 1 ].y = rcoord.y;
		vcoord[ 2 ].x = rcoord.x + rcoord.w;
		vcoord[ 2 ].y = rcoord.y + rcoord.h;
		vcoord[ 3 ].x = rcoord.x;
		vcoord[ 3 ].y = rcoord.y + rcoord.h;

		for( unsigned cnt=0; cnt<4; cnt++ )
		{
			fl_imgtk::draw_line( refimg, 
								 vcoord[ cnt ].x, vcoord[ cnt ].y, 
								 vcoord[ (cnt+1)%4 ].x, vcoord[ (cnt+1)%4 ].y, 
								 col );
		}
	}
}

static Fl_RGB_Image* getregionimg( Fl_RGB_Image* srcimg, LensTool::RectCoord &rcoord )
{
	if ( srcimg != nullptr )
	{
		return fl_imgtk::crop( srcimg, rcoord.x, rcoord.y, rcoord.w, rcoord.h );
	}
	
	return nullptr;
}

static uchar getmaxlevel( Fl_RGB_Image* srcimg )
{
	if (srcimg != nullptr)
	{
		unsigned img_w = srcimg->w();
		unsigned img_h = srcimg->h();
		unsigned img_d = srcimg->d();
		unsigned img_sz = img_w * img_h;

		uchar retlvl = 0;

		const uchar* pbuff = (const uchar*)srcimg->data()[ 0 ];

		for (size_t cnt = 0; cnt < img_sz; cnt++)
		{
			float av = 0.f;

			if (img_d == 1)
			{
				av += (float)( pbuff[ cnt ] );
			}
			else
			if (img_d >= 3)
			{
				av += (float)( pbuff[ cnt * img_d + 0 ] +
							   pbuff[ cnt * img_d + 1 ] +
							   pbuff[ cnt * img_d + 2 ] ) / 3.f;
			}

			if ( retlvl < (uchar)av )
			{
				retlvl = av;
			}
		}

		return retlvl;
	}

	return 0;
}

static float  getaveragegrey( Fl_RGB_Image* srcimg )
{
	if ( srcimg != nullptr )
	{
		unsigned img_w = srcimg->w();
		unsigned img_h = srcimg->h();
		unsigned img_d = srcimg->d();
		unsigned img_sz = img_w * img_h;

		float av = 0.0f;

		const uchar* pbuff = (const uchar*)srcimg->data()[0];

		for( size_t cnt=0; cnt<img_sz; cnt++ )
		{
			if ( img_d ==  1 )
			{
				av += (float)(pbuff[ cnt ]);
			}
			else
			if ( img_d >= 3 )
			{
				av += (float)( pbuff[ cnt * img_d + 0 ] +
							   pbuff[ cnt * img_d + 1 ] +
							   pbuff[ cnt * img_d + 2 ] ) / 3.f;
			}
		}

		av /= (float)img_sz;

		return av;
	}

	return 0.f;
}

static void gethistogramfromimg( Fl_RGB_Image* srcimg, LensTool::GrayHistogram &hist )
{
	if (srcimg != nullptr)
	{
		memset( &hist, 0, sizeof( LensTool::GrayHistogram ) );

		unsigned img_w = srcimg->w();
		unsigned img_h = srcimg->h();
		unsigned img_d = srcimg->d();
		unsigned img_sz = img_w * img_h;

		unsigned av = 0;
		const uchar* pbuff = (const uchar*)srcimg->data()[ 0 ];

		for (unsigned cnt = 0; cnt<img_sz; cnt++)
		{
			hist[ pbuff[ cnt * img_d ] ]++;
		}
	}
}

static void getrangeofhistogram( LensTool::GrayHistogram &hist, unsigned &minl, unsigned &maxl )
{
	minl = 0;
	maxl = 255;

	for (size_t cnt = 0; cnt <128; cnt++)
	{
		if ( hist[ cnt ] > THRESHOLD_HISTOGRAM_CUTOFF )
		{
			minl = cnt;
			break;
		}
	}

	for (size_t cnt = 255; cnt-- >128; )
	{
		if ( hist[ cnt ] > THRESHOLD_HISTOGRAM_CUTOFF )
		{
			maxl = cnt;
			break;
		}
	}

	if ( minl > maxl )
	{
		std::swap( maxl, minl );
	}

#ifdef DEBUG_GETCONTRAST
	printf( "getrangeofhistogram() -> max : %u, min : %u\n", maxl, minl );
#endif
}

static void removethesholdinhistogram( LensTool::GrayHistogram &hist, unsigned minl, unsigned width )
{
	for( size_t cnt=0; cnt<minl; cnt++ )
	{
		hist[ cnt ] = 0;
	}

	for( size_t cnt=255; cnt-- > (255-(width+minl)); )
	{
		hist[ cnt ] = 0;
	}
}

static void gethistrogramminmax( LensTool::GrayHistogram &hist, LensTool::HistoMinMax &minmax )
{
	minmax.maxv = 0;
	minmax.minv = 0xFFFFFFFF;

	//  find minimum and maximum.
	for (size_t cnt = 0; cnt <256; cnt++)
	{
		if (hist[ cnt ] > minmax.maxv)
		{
			minmax.maxv = hist[ cnt ];
		}

		if (hist[ cnt ] < minmax.minv)
		{
			minmax.minv = hist[ cnt ];
		}
	}
}


static void floodfillalgo( const uchar* refbuff, int x, int y, int w, int h, unsigned d,
						   vector < LensTool::LenstoolCoord > &refcoord, uchar threshold )
{
#define _FI_MACRO_PIXEL(_x_,_y_) refbuff[ ( _y_ * w + _x_ ) * d ]

	if (( x < 1 ) || ( y < 1 ) || ( x > w - 1 ) || ( y > h - 1 ))
		return;

	uchar curcol = _FI_MACRO_PIXEL( x, y );

	if (curcol < threshold)
	{
		queue< pair<int, int> > pointQ;
		pointQ.push( { x, y } );

		while (pointQ.empty() == false)
		{
			pair<int, int> aPair = pointQ.front();
			pointQ.pop();

			int px = aPair.first;
			int py = aPair.second;

			if (( px > 0 ) || ( py > 0 ) || ( px < w - 1 ) || ( py < h - 1 ))
			{
				uchar nowcol = _FI_MACRO_PIXEL( px, py );
				if (nowcol < threshold)
				{
					LensTool::LenstoolCoord newcoord = { px, py };
					// find same coordination for ignore or not.
					bool ffnd = false;
					for (size_t fcnt = 0; fcnt < refcoord.size(); fcnt++)
					{
						if (( refcoord[ fcnt ].x == px ) &&
							( refcoord[ fcnt ].y == py ))
						{
							ffnd = true;
							break;
						}
					}
					if (ffnd == false)
					{
						refcoord.push_back( newcoord );

						if ( px < ( w - 1 ) )
							pointQ.push( { px + 1, py } );
						
						if ( px > 0 )
							pointQ.push( { px - 1, py } );
						
						if( py < ( h - 1 ) )
							pointQ.push( { px, py + 1 } );

						if ( py > 0 )
							pointQ.push( { px, py - 1 } );
					}
				}
			}
		}
	}
}

}; /// of namespace LensTooltk;

using namespace LensTooltk;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LensTool::LensTool()
  : srcImg(nullptr),
	quadImg(nullptr),
	multiplier(4.0f),
	using1percenthistogramcutoff( false )
{
	threshold_lvl = (uchar)( 255.f * THRESHOLD_PASS_LIMIT );

	resetData();
}

LensTool::~LensTool()
{
	UnsetImage();
}

bool LensTool::SetImage( Fl_RGB_Image* img )
{
	if ( img != nullptr )
	{
		fl_imgtk::discard_user_rgb_image( srcImg );
		
		if (img->d() >= 3 )
		{
			srcImg = (Fl_RGB_Image*)img->copy();
		}
		else
		{
			srcImg = convert2rgb24( img );
		}
		
		multiplier = (float)img->w() / 200.f;

		findSubRegions();

		return true;
	}

	return false;
}

void LensTool::UnsetImage()
{
	if ( srcImg != nullptr )
	{
		fl_imgtk::discard_user_rgb_image( srcImg );
	}

	resetData();
}

void LensTool::GetRegionRect( unsigned index, RectCoord &arect )
{
	if ( index < MAXDATALEN )
	{
		arect.x = subrects[ index ].x;
		arect.y = subrects[ index ].y;
		arect.w = subrects[ index ].w;
		arect.h = subrects[ index ].h;
	}
}

bool LensTool::AnalyzeRects()
{
	if (srcImg != nullptr)
	{
		return findAnalysis();
	}

	return false;
}

void LensTool::GetAverages( GrayAverages &averages )
{
	for( unsigned cnt=0; cnt<TOTALARRAYSZ; cnt++ )
	{
		averages[ cnt ] = averagegray[ cnt ];
	}
}


bool LensTool::GetHistogram( unsigned index, GrayHistogram &hist, HistoMinMax &minmax )
{
	if ( index < MAXDATALEN )
	{
		memcpy( &hist, &grayhistogram, sizeof( GrayHistogram ) );
		
		minmax.maxv = grayhistominmax[ index ].maxv;
		minmax.minv = grayhistominmax[ index ].minv;

		return true;
	}

	return false;
}

float LensTool::GetContrast( unsigned index )
{
	if (index < MAXDATALEN)
	{
		unsigned maxv = grayhistominmax[ index ].maxv;
		unsigned minv = grayhistominmax[ index ].minv;

	#ifdef DEBUG_GETCONTRAST
		printf( "GetContrast[%u], max = %u, min = %u\n", index, maxv, minv );
	#endif /// DEBUG_GETCONTRAST
		float upperf = (float)maxv - (float)minv;
		float downf = (float)maxv + (float)minv;

		return upperf / downf;
	}

	return 0.f;
}

float LensTool::CalcContrast( float minf, float maxf )
{
	float upperf = (float)maxf - (float)minf;
	float downf  = (float)maxf + (float)minf;

	return upperf / downf;
}

bool LensTool::DrawAnalyzeRects( Fl_RGB_Image* img )
{
	if ( img != nullptr )
	{
		drawrectonimage( img, patternrect, 0xFF0000FF );
		drawrectonimage( img, actualpatternrect, 0x0000FFFF );

		for (size_t cnt = 0; cnt < 9; cnt++)
		{
			drawrectonimage( img, subrects[ cnt ], 0x00FF00FF );
		}

		for (size_t cnt = 0; cnt < 4; cnt++)
		{
			drawrectonimage( img, brightrects[ cnt ], 0xFFFF00FF );
		}

	#if LENSTOOL_VERSION < 2
		drawrectonimage( img, darkrect, 0x00FFFFFF );
	#endif /// of (LENSTOOL_VERION<2)
		return true;
	}

	return false;
}

void LensTool::SetHistoCutoff1Precent( bool onoff )
{
	using1percenthistogramcutoff = onoff;
}

void LensTool::Threshold( float thf )
{
	if ( thf >= 0.5f )
	{
		threshold_lvl = (uchar)( 255.f * thf );
	}
}

float LensTool::Threshold()
{
	return (float)threshold_lvl / 255.f;
}


bool LensTool::LoadPNG( const wchar_t* srcPNG )
{
	char convstr[1024] = {0,};

	wcstombs( convstr, srcPNG, 1024 );

	Fl_PNG_Image* imgpng = new Fl_PNG_Image( convstr );
	if ( imgpng != nullptr )
	{
		bool retb = SetImage( imgpng );

		delete imgpng;

		return retb;
	}

	return false;
}

void LensTool::resetData()
{
	for ( size_t cnt = 0; cnt < MAXDATALEN; cnt++)
	{
		memset( &subrects[ cnt ], 0, sizeof( RectCoord ) );
		memset( &subrects[ cnt ], 0, sizeof( RectCoord ) );
	}

	memset( &patternrect, 0, sizeof(RectCoord) );
	memset( &actualpatternrect, 0, sizeof(RectCoord) );
}

void LensTool::findSubRegions()
{
	if ( srcImg == nullptr )
		return;

	unsigned quadsz_w = (unsigned)((float)srcImg->w() / multiplier );
	unsigned quadsz_h = (unsigned)((float)srcImg->h() / multiplier );

	// Makes 1/4 bilinear filtered image.
	quadImg = fl_imgtk::rescale( srcImg, quadsz_w, quadsz_h, fl_imgtk::BILINEAR );

	if( quadImg != nullptr )
	{
		// Check maximum level;
		uchar maxl = getmaxlevel( quadImg );
		if ( maxl < 128 )
		{
			fl_imgtk::discard_user_rgb_image( quadImg );
			return;
		}
			
		// Detect inner region...
		RectCoord tmpCoord = { quadsz_w, quadsz_h, 0, 0 };
		vector< LenstoolCoord > findCoord;
		
		const uchar* refbuff = (const uchar*)quadImg->data()[0];

		floodfillalgo( refbuff, 
					   quadsz_w / 2, quadsz_h / 2,
					   quadsz_w, quadsz_h, quadImg->d(), 
					   findCoord, 128 );

		if ( findCoord.size() > 0 )
		{
			bool foundRect = false;

			// find min x,y and max w,h
			for( size_t cnt=0; cnt<findCoord.size(); cnt++ )
			{
				// Check min-X and max-X(w)
				if ( findCoord[cnt].x < tmpCoord.x )
				{
					tmpCoord.x = findCoord[ cnt ].x;
				}
				else
				if ( findCoord[cnt].x > tmpCoord.w )
				{
					tmpCoord.w = findCoord[ cnt ].x;
				}

				// Check min-Y and max-Y(h)
				if (findCoord[ cnt ].y < tmpCoord.y)
				{
					tmpCoord.y = findCoord[ cnt ].y;
				}
				else
				if (findCoord[ cnt ].y > tmpCoord.h)
				{
					tmpCoord.h = findCoord[ cnt ].y;
				}
			}

			// Check found min x,y and max w,h
			if ( ( tmpCoord.x < tmpCoord.w ) && ( tmpCoord.y < tmpCoord.h ) )
			{
				foundRect = true;
			}

			if ( foundRect == true )
			{
				// Change w and h to actual width and height.
				tmpCoord.w -= tmpCoord.x;
				tmpCoord.h -= tmpCoord.y;

				// Make factor multiply by multiplier.
				patternrect.x = tmpCoord.x * multiplier;
				patternrect.y = tmpCoord.y * multiplier;
				patternrect.w = tmpCoord.w * multiplier;
				patternrect.h = tmpCoord.h * multiplier;

				// Make 90% region of actual region.
				actualpatternrect.w = (unsigned)( (float)patternrect.w * 0.9f );
				actualpatternrect.h = (unsigned)( (float)patternrect.h * 0.9f );
				actualpatternrect.x = patternrect.x + 
									  ( ( patternrect.w - actualpatternrect.w ) / 2 );
				actualpatternrect.y = patternrect.y + 
					                  ( ( patternrect.h - actualpatternrect.h ) / 2 );

				// 3x3 
				float divided_w = (float)actualpatternrect.w / 3;
				float divided_h = (float)actualpatternrect.h / 3;
				float divin_w   = divided_w * 0.8f; /// 80% of divided rect width.
				float divin_h   = divided_h * 0.8f;
				
				unsigned mgn_l = (unsigned)( divided_w - divin_w ) / 2;
				unsigned mgn_t = (unsigned)( divided_h - divin_h ) / 2;

				for (size_t cnty = 0; cnty<3; cnty++)
				{
					for (size_t cntx = 0; cntx<3; cntx++)
					{
						unsigned sx = (unsigned)( divided_w * cntx );
						unsigned sy = (unsigned)( divided_h * cnty );

						size_t a_que = ( cnty * 3 ) + cntx;

						subrects[a_que].x = sx + mgn_l + actualpatternrect.x + 1;
						subrects[a_que].y = sy + mgn_t + actualpatternrect.y + 1;
						subrects[a_que].w = (unsigned)divin_w;
						subrects[a_que].h = (unsigned)divin_h;
					}
				}

				/*
				bright rects:
				        [0]
				    [1][   ][2]
				        [3]
				*/

				float brightgap_w = (float)srcImg->w() * 0.018f;
				float brightgap_h = (float)srcImg->h() * 0.018f;
				float brightrect_w = (float)srcImg->w() * 0.015f;
				float brightrect_h = (float)srcImg->h() * 0.015f;

				brightrects[0].x = actualpatternrect.x;
				brightrects[0].y = patternrect.y - (unsigned)brightgap_h - (unsigned)brightrect_h;
				brightrects[0].w = actualpatternrect.w;
				brightrects[0].h = (unsigned)brightrect_h;

				brightrects[1].x = patternrect.x - (unsigned)brightgap_w - (unsigned)brightrect_w;
				brightrects[1].y = actualpatternrect.y;
				brightrects[1].w = (unsigned)brightrect_w;
				brightrects[1].h = actualpatternrect.h;
				
				brightrects[2].x = patternrect.x + patternrect.w + (unsigned)brightgap_w;
				brightrects[2].y = actualpatternrect.y;
				brightrects[2].w = (unsigned)brightrect_w;
				brightrects[2].h = actualpatternrect.h;

				brightrects[3].x = actualpatternrect.x;
				brightrects[3].y = patternrect.y + patternrect.h + (unsigned)brightgap_h;
				brightrects[3].w = actualpatternrect.w;
				brightrects[3].h = (unsigned)brightrect_h;

			#if LENSTOOL_VERSION < 2
				darkrect.x = actualpatternrect.x;
				darkrect.y = srcImg->h() - (unsigned)brightrect_h - (unsigned)brightgap_h;
				darkrect.w = actualpatternrect.w;
				darkrect.h = (unsigned)brightrect_h;
			#endif /// of (LENSTOOL_VERION<2)
			}

			// Discard used finding coordinations.
			findCoord.clear();
		}

	// DEBUG ---------------------------------
	#ifdef DEBUG_IMG_TRACE
		drawrectonimage( quadImg, tmpCoord, 0xFF0000FF );
		WritePNGFromFlImage( "QAUD_REGION_TRACE.PNG", quadImg );

		Fl_RGB_Image* dbgImgBig = (Fl_RGB_Image*)srcImg->copy();
		if (dbgImgBig != nullptr )
		{
			drawrectonimage( dbgImgBig, patternrect, 0xFF0000FF );
			drawrectonimage( dbgImgBig, actualpatternrect, 0x0000FFFF );

			for( size_t cnt=0; cnt<9; cnt++ )
			{
				drawrectonimage( dbgImgBig, subrects[cnt], 0x00FF00FF );
			}
			
			for (size_t cnt = 0; cnt<4; cnt++)
			{
				drawrectonimage( dbgImgBig, brightrects[cnt], 0xFFFF00FF );
			}
		#if (LENSTOOL_VERION<2)
			drawrectonimage( dbgImgBig, darkrect, 0x00FFFFFF );
		#endif /// of (LENSTOOL_VERION<2)
			WritePNGFromFlImage( "REGION_TRACE.PNG", dbgImgBig );
			fl_imgtk::discard_user_rgb_image( dbgImgBig );
		}
	#endif /// of DEBUG_IMG_TRACE
	// DEBUG ---------------------------------

		fl_imgtk::discard_user_rgb_image( quadImg );
	}
}

bool LensTool::findAnalysis()
{
	if ( srcImg == nullptr )
		return false;

	for (size_t cnt = 0; cnt < 9; cnt++)
	{
		// test sub-rectangle valid
		bool rectValid = true;

		if ( ( subrects[ cnt ].x == 0 ) || ( subrects[ cnt ].y == 0 ) )
			rectValid = false;

		if ( ( subrects[ cnt ].w == 0 ) || ( subrects[ cnt ].h == 0 ) )
			rectValid = false;

		if ( rectValid == true )
		{
			Fl_RGB_Image* imgCropped = fl_imgtk::crop( srcImg,
													   subrects[cnt].x,
													   subrects[cnt].y,
													   subrects[cnt].w,
													   subrects[cnt].h );
			if ( imgCropped != nullptr )
			{
				averagegray[cnt] = getaveragegrey( imgCropped );
				gethistogramfromimg( imgCropped, grayhistogram[cnt] );
				
				unsigned minl = 0;
				unsigned maxl = 0;

				getrangeofhistogram( grayhistogram[ cnt ], minl, maxl );

				unsigned drange = maxl - minl;
				unsigned dgap   = 0;
				unsigned comin  = 0;
				unsigned comax  = 0;

				if ( using1percenthistogramcutoff == true )
				{
					dgap = (unsigned)( (float)drange * 0.01f );
					comin = minl + dgap;
					comax = minl + ( drange - dgap );
				}
				else
				{
					comax = maxl;
					comin = minl;
				}
				
				grayhistominmax[ cnt ].maxv = comax;
				grayhistominmax[ cnt ].minv = comin;

				fl_imgtk::discard_user_rgb_image( imgCropped );
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	for (size_t cnt = 0; cnt < 4; cnt++)
	{
		// test sub-rectangle valid
		bool rectValid = true;

		if (( brightrects[ cnt ].x == 0 ) || ( brightrects[ cnt ].y == 0 ))
			rectValid = false;

		if (( brightrects[ cnt ].w == 0 ) || ( brightrects[ cnt ].h == 0 ))
			rectValid = false;

		if (rectValid == true)
		{
			Fl_RGB_Image* imgCropped = fl_imgtk::crop( srcImg,
													   brightrects[ cnt ].x,
													   brightrects[ cnt ].y,
													   brightrects[ cnt ].w,
													   brightrects[ cnt ].h );
			if (imgCropped != nullptr)
			{
				averagegray[ 9 + cnt ] = getaveragegrey( imgCropped );

				fl_imgtk::discard_user_rgb_image( imgCropped );
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

#if LENSTOOL_VERSION < 2
	if (( darkrect.x == 0 ) || ( darkrect.y == 0 ))
		return false;

	if (( darkrect.w == 0 ) || ( darkrect.h == 0 ))
		return false;

	Fl_RGB_Image* imgCropped = fl_imgtk::crop( srcImg,
											   darkrect.x,
											   darkrect.y,
											   darkrect.w,
											   darkrect.h );
	if (imgCropped != nullptr)
	{
		averagegray[ 13 ] = getaveragegrey( imgCropped );

		fl_imgtk::discard_user_rgb_image( imgCropped );
	}
	else
	{
		return false;
	}
	#endif /// of (LENSTOOL_VERION<2)

	return true;
}
