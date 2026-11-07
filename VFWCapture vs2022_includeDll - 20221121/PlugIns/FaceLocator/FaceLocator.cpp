// FaceLocator.cpp : Defines the initialization routines for the DLL.
#include "stdafx.h"
#include "FaceLocator.h"
#include "BufStruct.h"
#include "ImageProc.h"
#include "FaceSegmentation.h"

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
// CFaceLocatorApp
BEGIN_MESSAGE_MAP(CFaceLocatorApp, CWinApp)
	//{{AFX_MSG_MAP(CFaceLocatorApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
/////////////////////////////////////////////////////////////////////////////
// CFaceLocatorApp construction
CFaceLocatorApp::CFaceLocatorApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}
/////////////////////////////////////////////////////////////////////////////
// The one and only CFaceLocatorApp object
CFaceLocatorApp theApp;
char sInfo[] = "Face localization and jaw-line refinement";
bool bLastPlugin = false;
DLL_EXP void ON_PLUGIN_BELAST(bool bLast)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState()); // 切换到插件模块状态。
	bLastPlugin = bLast;
}
DLL_EXP LPCTSTR ON_PLUGININFO(void)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState()); // 切换到插件模块状态。
	return sInfo;
}
DLL_EXP void ON_INITPLUGIN(LPVOID lpParameter)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState()); // 切换到插件模块状态。
	//theApp.dlg.Create(IDD_PLUGIN_SETUP);
	//theApp.dlg.ShowWindow(SW_HIDE);
}
DLL_EXP int ON_PLUGINCTRL(int nMode,void* pParameter)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	int nRet = 0;
	switch(nMode)
	{
	case 0:
		{
			//theApp.dlg.ShowWindow(SW_SHOWNORMAL);
			//theApp.dlg.SetWindowPos(NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
		}
		break;
	}
	return nRet;
}
/****************************************************************************************/
/*                              人脸分割与定位                                          */
/****************************************************************************************/

// 只接受共享缓冲区布局支持的图像尺寸。
DLL_EXP void ON_PLUGINRUN(int w, int h, BYTE* pYBits, BYTE* pUBits,
                          BYTE* pVBits, BYTE* pBuffer)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (!pBuffer || !pYBits || w <= 0 || h <= 0 ||
        w % 16 != 0 || h % 4 != 0)
        return;

    BUF_STRUCT* bs = (BUF_STRUCT*)pBuffer;
    if (bs->bNotInited || bs->W != w || bs->H != h ||
        !bs->clrBmp_1d8 || !bs->TempImage1d8 || !bs->colorBmp)
        return;

    bs->rcnFace.left = bs->rcnFace.top = 0;
    bs->rcnFace.width = bs->rcnFace.height = 0;
    bs->nFacePixelNum = 0;
    const int maskW = w / 4;
    const int maskH = h / 4;
    const int yCount = (w / 2) * maskH;
    const int uvCount = maskW * maskH;
    const BYTE* u = bs->clrBmp_1d8 + yCount;
    const BYTE* v = u + uvCount;
    // 在覆盖降采样 Y 平面之前完成定位，保留原始亮度和色度证据。
    FaceSegmentation::BaselineStages stages{};
    FaceSegmentation::Result face = FaceSegmentation::RunJawRefined(
        u, v, maskW, maskH, bs->colorBmp, nullptr, &stages);

    memcpy(bs->TempImage1d8, face.mask.data(), uvCount);
    // 将最终 mask 写入降采样 Y 平面，U/V 平面保持不变。
    BYTE* reducedY = bs->clrBmp_1d8;
    for (int y = 0; y < maskH; ++y)
        for (int x = 0; x < maskW; ++x)
        {
            const BYTE value = face.mask[y * maskW + x];
            reducedY[y * (w / 2) + x * 2] = value;
            reducedY[y * (w / 2) + x * 2 + 1] = value;
        }

    if (face.pixelCount > 0)
    {
        bs->rcnFace.left = face.left;
        bs->rcnFace.top = face.top;
        bs->rcnFace.width = face.width;
        bs->rcnFace.height = face.height;
        bs->nFacePixelNum = face.pixelCount;

        // 将降采样坐标换算为原始视频坐标。
        aRect original = { face.left * 2, face.top * 4,
                           face.width * 2, face.height * 4 };
        DrawRectangle(pYBits, w, h, original, RGB(235, 16, 240), false);
    }

    // 在画面顶部依次显示阈值、开运算、闭运算和最终 mask。
    CopyToRect(stages.threshold.data(), pYBits, maskW, maskH, w, h,
               0, 0, true);
    CopyToRect(stages.opened.data(), pYBits, maskW, maskH, w, h,
               maskW, 0, true);
    CopyToRect(stages.closed.data(), pYBits, maskW, maskH, w, h,
               maskW * 2, 0, true);
    CopyToRect(bs->TempImage1d8, pYBits, maskW, maskH,
               w, h, w - maskW, 0, true);
}
DLL_EXP void ON_PLUGINEXIT()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState()); // 切换到插件模块状态。
	//theApp.dlg.DestroyWindow();
}

