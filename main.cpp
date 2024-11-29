#include <mod/amlmod.h>
#include <mod/logger.h>
#include "isautils.h"

#ifdef AML32
    #include "GTASA_STRUCTS.h"
#else
    #include "GTASA_STRUCTS_210.h"
#endif

MYMOD(net.erorcun.rusjj.lcssnow, LCSSnow, 1.1, erorcun & RusJJ)
NEEDGAME(com.rockstargames.gtasa)

#define SQUEEZE_FPS

struct OneSnowFlake
{
    CVector pos;
    float xChange;
    float yChange;
};
RwRGBA rwRgbaWhite(0xFF, 0xFF, 0xFF, 0xFF);

// Savings
uintptr_t pGTASA;
void* hGTASA;
ISAUtils* sautils;

float Snow, TargetSnow = 1;
float TurnOffTime;
CBox SnowBox;
CMatrix SnowMat;
RwTexture* SnowFlakeTexture = NULL;
RwRaster* SnowFlakeRaster = NULL;

bool SnowFlakesInitialised = false, SnowVisible = true;
OneSnowFlake* SnowFlakesArray = NULL;
int MaxSnowFlakes = 2000, CurrentSnowFlakes = 0.75f * MaxSnowFlakes;

// Game Vars
CCamera *TheCamera;
float *InterpolationValue, *UnderWaterness, *ms_fTimeStep;

// Game Funcs
bool (*CamNoRain)();
bool (*PlayerNoRain)();
void (*RwRenderStateSet)(RwRenderState, void*);
RwTexture* (*RwTextureRead)(const char*, const char*);
bool (*RwIm3DTransform)(RwIm3DVertex*, uint32_t, RwMatrix*, uint32_t);
void (*RwIm3DRenderIndexedPrimitive)(RwPrimitiveType, uint16_t*, int);
void (*RwIm3DEnd)();

// Funcs
inline float ClampFloat(float value, float min, float max)
{
    if(value > max) return max;
    if(value < min) return min;
    return value;
}
inline void AddSnow()
{
    if(TargetSnow != 0 || Snow != 0)
    {
        if(TargetSnow == 0)
        {
            Snow -= 0.25f * (*InterpolationValue - 4.0f * TurnOffTime);
            if(Snow < 0) Snow = 0;
        }
        else
        {
            Snow = *InterpolationValue * 4.0f;
            if(Snow > 2)
            {
                Snow -= 2.0f * (Snow - 2.0f);
            }
            if(Snow < 0) Snow = 0;
            else if(Snow > TargetSnow) Snow = TargetSnow;
        }
    }

    if(!CamNoRain() && !PlayerNoRain() && *UnderWaterness <= 0)
    {
        int SnowAmount = fmin(CurrentSnowFlakes, Snow * CurrentSnowFlakes);

        CVector& camPos = TheCamera->GetPosition();
        SnowBox.m_vecMin = { camPos.x - 40.0f, camPos.y - 40.0f, camPos.z - 15.0f };
        SnowBox.m_vecMax = { camPos.x + 40.0f, camPos.y + 40.0f, camPos.z + 15.0f };

        if(!SnowFlakesInitialised)
        {
            SnowFlakesInitialised = true;
            for(int i = 0; i < MaxSnowFlakes; ++i)
            {
                SnowFlakesArray[i].pos.x = SnowBox.m_vecMin.x + ((SnowBox.m_vecMax.x - SnowBox.m_vecMin.x) * (rand() / (float)RAND_MAX));
                SnowFlakesArray[i].pos.y = SnowBox.m_vecMin.y + ((SnowBox.m_vecMax.y - SnowBox.m_vecMin.y) * (rand() / (float)RAND_MAX));
                SnowFlakesArray[i].pos.z = SnowBox.m_vecMin.z + ((SnowBox.m_vecMax.z - SnowBox.m_vecMin.z) * (rand() / (float)RAND_MAX));

                SnowFlakesArray[i].xChange = 0;
                SnowFlakesArray[i].yChange = 0;
            }
        }

        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)0);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, SnowFlakeRaster);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)0);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)2); // erorcun: 1 in psp, 5 in mobile
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)2); // erorcun: 1 in psp, 6 in mobile

        // Render part
        SnowMat = *TheCamera->GetMatrix();
        for(int i = 0; i < SnowAmount; ++i)
        {
            static uint16_t snowRenderOrder[] = { 0, 1, 2, 3, 4, 5 };
            static RwIm3DVertex snowVertexBuffer[] =
            {
                { RwV3d {  0.07, 0.00,  0.07 }, RwV3d(), rwRgbaWhite, 1.0f, 1.0f },
                { RwV3d { -0.07, 0.00,  0.07 }, RwV3d(), rwRgbaWhite, 0.0f, 1.0f },
                { RwV3d { -0.07, 0.00, -0.07 }, RwV3d(), rwRgbaWhite, 0.0f, 0.0f },
                { RwV3d {  0.07, 0.00,  0.07 }, RwV3d(), rwRgbaWhite, 1.0f, 1.0f },
                { RwV3d {  0.07, 0.00, -0.07 }, RwV3d(), rwRgbaWhite, 1.0f, 0.0f },
                { RwV3d { -0.07, 0.00, -0.07 }, RwV3d(), rwRgbaWhite, 0.0f, 0.0f },
            };

            float& xPos = SnowFlakesArray[i].pos.x;
            float& yPos = SnowFlakesArray[i].pos.y;
            float& zPos = SnowFlakesArray[i].pos.z;
            float& xChangeRate = SnowFlakesArray[i].xChange;
            float& yChangeRate = SnowFlakesArray[i].yChange;

            float minChange = -0.03f * *ms_fTimeStep;
            float maxChange =  0.03f * *ms_fTimeStep;

            #ifdef SQUEEZE_FPS
                static float ran1 = -0.08f, ran2 = 0.08f;
                static uint8_t iflakes = 0;
                if((++iflakes) % 64 == 0)
                {
                    ran1 = minChange + (2 * maxChange * (rand() / (float)RAND_MAX));
                    ran2 = minChange + (2 * maxChange * (rand() / (float)RAND_MAX));
                    iflakes = 0;
                }
                zPos -= maxChange;
                xChangeRate += ran1;
                yChangeRate += ran2;
            #else
                zPos -= maxChange;
                xChangeRate += minChange + (2 * maxChange * (rand() / (float)RAND_MAX));
                yChangeRate += minChange + (2 * maxChange * (rand() / (float)RAND_MAX));
            #endif

            xChangeRate = ClampFloat(xChangeRate, minChange, maxChange);
            yChangeRate = ClampFloat(yChangeRate, minChange, maxChange);

            xPos += xChangeRate;
            yPos += yChangeRate;

            while (zPos < SnowBox.m_vecMin.z) zPos += 30.0f; // += 20.0f; in PSP
            while (zPos > SnowBox.m_vecMax.z) zPos -= 30.0f; // -= 20.0f; in PSP
            while (xPos < SnowBox.m_vecMin.x) xPos += 80.0f;
            while (xPos > SnowBox.m_vecMax.x) xPos -= 80.0f;
            while (yPos < SnowBox.m_vecMin.y) yPos += 80.0f;
            while (yPos > SnowBox.m_vecMax.y) yPos -= 80.0f;

            SnowMat.GetPosition() = SnowFlakesArray[i].pos;
            if (RwIm3DTransform(snowVertexBuffer, 6, (RwMatrix*)&SnowMat, 1))
            {
                // PSP doesn't do it in indexed fashion, but this is only function we have in III/VC.
                RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, snowRenderOrder, 6);
                RwIm3DEnd();
            }
        }

        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)1);

        // erorcun: my addition
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)5);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)6);
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, 0);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, 0);
    }
}
void OnSnowDensityChanged(int oldVal, int newVal, void* data)
{
    clampint(0, 3, &newVal);
    switch(newVal)
    {
        case 0:
            CurrentSnowFlakes = 0.15f * MaxSnowFlakes;
            break;
        case 1:
            CurrentSnowFlakes = 0.40f * MaxSnowFlakes;
            break;
        default:
            CurrentSnowFlakes = 0.70f * MaxSnowFlakes;
            break;
        case 3:
            CurrentSnowFlakes =         MaxSnowFlakes;
            break;
    }
    aml->MLSSetInt("SNWDNS", newVal);
}
void OnSnowVisibilityChanged(int oldVal, int newVal, void* data)
{
    SnowVisible = (newVal!=0);
}


// Hooks
DECL_HOOKv(GameInit2)
{
    GameInit2();

    SnowFlakeTexture = RwTextureRead("shad_exp", NULL);
    SnowFlakeRaster = SnowFlakeTexture->raster;
}
DECL_HOOKv(RenderRainStreaks)
{
    if(SnowVisible) AddSnow();
    RenderRainStreaks();
}

// int main!
extern "C" void OnModLoad()
{
    logger->SetTag("LCSSnowMobile");
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    // Hooks
    HOOKPLT(GameInit2, pGTASA + BYBIT(0x672178, 0x843A80));
    HOOKPLT(RenderRainStreaks, pGTASA + BYBIT(0x673BD0, 0x846540));

    // Getters
    SET_TO(TheCamera, aml->GetSym(hGTASA, "TheCamera"));
    SET_TO(InterpolationValue, aml->GetSym(hGTASA, "_ZN8CWeather18InterpolationValueE"));
    SET_TO(UnderWaterness, aml->GetSym(hGTASA, "_ZN8CWeather14UnderWaternessE"));
    SET_TO(ms_fTimeStep, aml->GetSym(hGTASA, "_ZN6CTimer12ms_fTimeStepE"));

    SET_TO(CamNoRain, aml->GetSym(hGTASA, "_ZN10CCullZones9CamNoRainEv"));
    SET_TO(PlayerNoRain, aml->GetSym(hGTASA, "_ZN10CCullZones12PlayerNoRainEv"));
    SET_TO(RwRenderStateSet, aml->GetSym(hGTASA, "_Z16RwRenderStateSet13RwRenderStatePv"));
    SET_TO(RwTextureRead, aml->GetSym(hGTASA, "_Z13RwTextureReadPKcS0_"));
    SET_TO(RwIm3DTransform, aml->GetSym(hGTASA, "_Z15RwIm3DTransformP18RxObjSpace3DVertexjP11RwMatrixTagj"));
    SET_TO(RwIm3DRenderIndexedPrimitive, aml->GetSym(hGTASA, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti"));
    SET_TO(RwIm3DEnd, aml->GetSym(hGTASA, "_Z9RwIm3DEndv"));

    // MLS
    sautils = (ISAUtils*)GetInterface("SAUtils");
    if(sautils)
    {
        static const char* aDensityQuality[4] = 
        {
            "FED_FXL",
            "FED_FXM",
            "FED_FXH",
            "FED_FXV",
        };
        static const char* aVisibleQuality[2] = 
        {
            "FEM_OFF",
            "FEM_ON",
        };
        int snowDensity = 2;
        aml->MLSGetInt("SNWDNS", &snowDensity); clampint(0, 3, &snowDensity);
        if(snowDensity != 2) OnSnowDensityChanged(2, snowDensity, NULL);
        sautils->AddClickableItem(eTypeOfSettings::SetType_Display, "Snow Density", snowDensity, 0, 3, aDensityQuality, OnSnowDensityChanged, NULL);
        sautils->AddClickableItem(eTypeOfSettings::SetType_Display, "Draw Snow", SnowVisible, 0, 1, aVisibleQuality, OnSnowVisibilityChanged, NULL);
    }

    // Post-init
    SnowFlakesArray = new OneSnowFlake[MaxSnowFlakes];
}
