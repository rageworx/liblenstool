#ifndef __LENSTOOL_H__
#define __LENSTOOL_H__

#if (_MSC_VER > 600 )
	#pragma once
	#pragma warning(disable : 4996)  
	#define _CRT_NONSTDC_NO_WARNINGS
#endif

#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_RGB_Image.H>

#include <vector>

////////////////////////////////////////////////////////////////////////////////

#define LENSTOOL_VERSION	2

////////////////////////////////////////////////////////////////////////////////

class LensTool
{
	public:
		typedef struct _LenstoolCoord
		{
			unsigned x;
			unsigned y;
		}LenstoolCoord;

		typedef struct _RectCoord
		{
			unsigned x;
			unsigned y;
			unsigned w;
			unsigned h;
		}RectCoord;
		
		typedef struct _HistoMinMax
		{
			unsigned maxv;
			unsigned minv;
		}HistoMinMax;

	#if LENSTOOL_VERSION == 2
		// 3x3 + bright 4 == 13
		#define				TOTALARRAYSZ		13
	#else
		// 3x3 + bright 4 + dark 1 == 14
		#define				TOTALARRAYSZ		14
	#endif /// of LENSTOOL_VERSION == 2 
		typedef float		GrayAverages[ TOTALARRAYSZ ];
		typedef	unsigned	GrayHistogram[ 256 ];

	public:
		LensTool();
		virtual ~LensTool();

	public:
		bool SetImage( Fl_RGB_Image* img = nullptr );
		void UnsetImage();
		void GetRegionRect( unsigned index, RectCoord &arect );
		bool AnalyzeRects();
		void GetAverages( GrayAverages &averages );
		bool GetHistogram( unsigned index, GrayHistogram &hist, HistoMinMax &minmax );
		float GetContrast( unsigned index );
		static float CalcContrast( float minf, float maxf );
		bool DrawAnalyzeRects( Fl_RGB_Image* img );
		void SetHistoCutoff1Precent( bool onoff );
		void Threshold( float thf ); /// 0.5 ~ 1.0f ( 50 ~ 100% )
		float Threshold(); 

	public:
		bool LoadPNG( const wchar_t* srcPNG );

	protected:
		void resetData();
		void findSubRegions();
		bool findAnalysis();

	protected:
		#define				MAXDATALEN			9	/// 3x3 matrix subrects.
		RectCoord			patternrect;
		RectCoord			actualpatternrect;
		RectCoord			subrects[ MAXDATALEN ];
		RectCoord			brightrects[ 4 ];
	#if LENSTOOL_VERSION < 2
		RectCoord			darkrect;
	#endif  /// of LENSTOOL_VERSION < 2
		GrayAverages		averagegray;
		GrayHistogram		grayhistogram[ MAXDATALEN ];
		HistoMinMax			grayhistominmax[ MAXDATALEN ];

	protected:
		uchar			threshold_lvl;
		Fl_RGB_Image*	srcImg;
		Fl_RGB_Image*	quadImg;
		float			multiplier;
		bool			using1percenthistogramcutoff;
};

#endif /// of __LENSTOOL_H__