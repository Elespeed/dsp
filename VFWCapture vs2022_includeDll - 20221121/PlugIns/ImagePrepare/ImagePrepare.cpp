// ImagePrepare.cpp : Defines the initialization routines for the DLL.

#include "stdafx.h"
#include "ImagePrepare.h"
//
#include "BufStruct.h"
#include "ImageProc.h"
#include "math.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//	Note!
//
//		If this DLL is dynamically linked against the MFC
//		DLLs, any functions exported from this DLL which
//		call into MFC must have the AFX_MANAGE_STATE macro
//		added at the very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the 
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//
/////////////////////////////////////////////////////////////////////////////
// CImagePrepareApp
BEGIN_MESSAGE_MAP(CImagePrepareApp, CWinApp)
	//{{AFX_MSG_MAP(CImagePrepareApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
//
/////////////////////////////////////////////////////////////////////////////
// CImagePrepareApp construction
CImagePrepareApp::CImagePrepareApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}
/////////////////////////////////////////////////////////////////////////////
// The one and only CImagePrepareApp object
CImagePrepareApp theApp;
//extern "C"__declspec(dllimport)表示，兼容C方式导入函数
DLL_INP void InitFeatureBuffer(BUF_STRUCT* pBS);
DLL_INP void InitFeatureVector(FeatureVector* pThis);

char sInfo[] = "人脸跟踪-摄像头视频流图片截取处理插件";
bool bLastPlugin = false;
//extern "C" _declspec(dllexport)表示，兼容C方式导出函数
//bLastPlugin为真时，表示为最后一个有效插件
DLL_EXP void ON_PLUGIN_BELAST(bool bLast)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());//模块状态切换
	bLastPlugin = bLast;
}
//插件名称
DLL_EXP LPCTSTR ON_PLUGININFO(void)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());//模块状态切换
	return sInfo;
}
//
DLL_EXP void ON_INITPLUGIN(LPVOID lpParameter)
{   
	AFX_MANAGE_STATE(AfxGetStaticModuleState());//模块状态切换
	//theApp.dlg.Create(IDD_PLUGIN_SETUP);
	//theApp.dlg.ShowWindow(SW_HIDE);
}
DLL_EXP int ON_PLUGINCTRL(int nMode,void* pParameter)
{
//模块状态切换
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	int nRet = 0;
	return nRet;
}
//初始化跟踪体
void InitTraceObject(TRACE_OBJECT* pThis,char name[8])
{
        memset(&pThis->rcObject,0,sizeof(aRect));//跟踪体区域
        pThis->spdxObj = pThis->spdyObj = 0;//跟踪体运动方向、速度
        pThis->nMinDist = 0x7fffffff;//最小特征差距
        pThis->bBrokenTrace = false;//跟踪失败标记
        pThis->nBrokenTimes = 0;//疑似丢失跟踪次数
        pThis->bSaveit= false;//丢失跟踪标记
		//初始化跟踪体特征向量
        InitFeatureVector(&pThis->fvObject);//当前特征
        InitFeatureVector(&pThis->fvObject_org);//原始特征
		strncpy(pThis->sName,name,8);
		pThis->sName[7] = 0;
}

DLL_EXP void ON_PLUGINRUN(int w, int h, BYTE* pYBits, BYTE* pUBits,
                          BYTE* pVBits, BYTE* pBuffer)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (!pBuffer || !pYBits || !pUBits || !pVBits ||
        w <= 0 || h <= 0 || w % 16 != 0 || h % 4 != 0)
        return;

    BUF_STRUCT* bs = (BUF_STRUCT*)pBuffer;
    const size_t pixels = (size_t)w * h;
    const size_t capacity = pixels * 4;
    const size_t colorBytes = pixels * 2;
    const size_t reducedBytes = pixels / 4;
    const size_t grayBytes = pixels / 16;
    const size_t maskBytes = pixels / 16;
    const size_t fixedBytes = sizeof(BUF_STRUCT) + colorBytes +
        pixels + reducedBytes + grayBytes + maskBytes + sizeof(OTHER_VARS);
    if (fixedBytes > capacity)
        return;

    if (bs->bNotInited || bs->W != w || bs->H != h)
    {
        // 共享缓冲区以 BUF_STRUCT 开头，所有指针均指向其后固定区域。
        memset(bs, 0, sizeof(BUF_STRUCT));
        BYTE* next = pBuffer + sizeof(BUF_STRUCT);
        bs->W = w;
        bs->H = h;
        bs->colorBmp = next;
        next += colorBytes;
        bs->grayBmp = next;
        next += pixels;
        bs->clrBmp_1d8 = next;
        next += reducedBytes;
        bs->grayBmp_1d16 = next;
        next += grayBytes;
        bs->TempImage1d8 = next;
        next += maskBytes;
        bs->pOtherVars = (OTHER_VARS*)next;
        next += sizeof(OTHER_VARS);
        memset(bs->pOtherVars, 0, sizeof(OTHER_VARS));
        bs->pOtherData = next;
        bs->max_allocSize = (int)(capacity - (next - pBuffer));
        myHeapAllocInit(bs);
        InitFeatureBuffer(bs);
        bs->bNotInited = false;
        ShowDebugMessage("ImagePrepare 初始化：原图 %dx%d，降采样 %dx%d，色度 %dx%d",
            w, h, w / 2, h / 4, w / 4, h / 4);
    }

    // 显示图引用当前帧；固定缓存分别保存原图与降采样结果。
    bs->displayImage = pYBits;
    memcpy(bs->colorBmp, pYBits, pixels);
    memcpy(bs->colorBmp + pixels, pUBits, pixels / 2);
    memcpy(bs->colorBmp + pixels + pixels / 2, pVBits, pixels / 2);
    memcpy(bs->grayBmp, pYBits, pixels);

    const int smallW = w / 2;
    const int smallH = h / 4;
    const int yCount = smallW * smallH;
    const int uvCount = yCount / 2;
    BYTE* outY = bs->clrBmp_1d8;
    BYTE* outU = outY + yCount;
    BYTE* outV = outU + uvCount;
    for (int y = 0; y < smallH; ++y)
    {
        const int sourceY = y * 4;
        for (int x = 0; x < smallW; ++x)
            outY[y * smallW + x] = pYBits[sourceY * w + x * 2];
        for (int x = 0; x < smallW / 2; ++x)
        {
            const int sourceIndex = sourceY * (w / 2) + x * 2;
            outU[y * (smallW / 2) + x] = pUBits[sourceIndex];
            outV[y * (smallW / 2) + x] = pVBits[sourceIndex];
        }
    }

    for (int y = 0; y < h / 4; ++y)
        for (int x = 0; x < w / 4; ++x)
            bs->grayBmp_1d16[y * (w / 4) + x] =
                pYBits[(y * 4) * w + x * 4];

    // 单独激活预处理插件时，在右上角展示降采样结果以核对坐标。
    if (bLastPlugin)
        CopyToRect(bs->clrBmp_1d8, pYBits, smallW, smallH,
                   w, h, w - smallW, 0, false);
}
DLL_EXP void ON_PLUGINEXIT()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());//模块状态切换
	//theApp.dlg.DestroyWindow();
}
