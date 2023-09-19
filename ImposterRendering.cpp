/*
* Copyright (c) 2017-2023 The Forge Interactive Inc.
*
* This file is part of The-Forge
* (see https://github.com/ConfettiFX/The-Forge).
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*/

/********************************************************************************************************
*
* Imposter Rendering
* Sangmin Kim
* The purpose of this demo is present imposter rendering method that I implemented.
*
*********************************************************************************************************/

#include "Shaders/Shared.h"

// Interfaces
#include "../../../../Common_3/Application/Interfaces/ICameraController.h"
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Application/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"

// Rendering
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Middleware packages
#include "../../../../Common_3/Resources/AnimationSystem/Animation/SkeletonBatcher.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/AnimatedObject.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Animation.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Clip.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/ClipController.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Rig.h"

// Math
#include "../../../../Common_3/Utilities/Math/MathTypes.h"

// Memory
#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

#define TextureCount 180
#define ImposterCountPerGroup 10000
#define MaxImposterCount 200000

////////////////////////////////////////////////////////////////////////////////////
//									Root Constant Blocks						  //
////////////////////////////////////////////////////////////////////////////////////

struct MatrixBlock
{
	mat4 mProjMat;
	mat4 mViewMat;
	mat4 mToWorldMat;
}projViewModelMatrices;

/// @brief rootConstant block for billboardsRootConstant.
struct billboardsRootConstant
{
	float4 camPos;
	float4 lightPos;
	int showQuads;
	int genShadow;
	int frustumOn;
	int imposter360;
	int imposterCount;
}billboardRootConstantBlock;

struct UniformDataBones
{
	mat4 mBoneMatrix[MAX_NUM_BONES];
}gUniformDataBones;

////////////////////////////////////////////////////////////////////////////////////
//									Buffer - Wrap struct					      //
////////////////////////////////////////////////////////////////////////////////////
struct MyBuffer
{
	//Update & Read data
	void UpdateData(void* source)
	{
		BufferUpdateDesc updateDesc = { buffer };
		beginUpdateResource(&updateDesc);
		memcpy(updateDesc.pMappedData, source, size);
		endUpdateResource(&updateDesc, NULL);
	}
	void ReadData(void* dst)
	{
		BufferUpdateDesc updateDesc = { buffer };
		beginUpdateResource(&updateDesc);
		memcpy(dst, updateDesc.pMappedData, size);
		endUpdateResource(&updateDesc, NULL);
	}
	uint64_t size;
	Buffer* buffer;
};

////////////////////////////////////////////////////////////////////////////////////
//									Setups										  //
////////////////////////////////////////////////////////////////////////////////////
Renderer* renderer = NULL;
SwapChain* pSwapChain = NULL;
Queue* queue = NULL;
GpuCmdRing* gGraphicsCmdRing = NULL;
Semaphore* pImageAcquiredSemaphore = NULL;

//Geoms
Geometry* pGeom = NULL;
GeometryData* pGeomData = NULL;

//Textures
Sampler* pDefaultSampler = NULL;
Sampler* pShadowSampler = NULL;
Texture* pTextureDiffuse = NULL;

//Anim File Paths
const char* gStickFigureName = "stormtrooper/skeleton.ozz";
const char* gClipName = "stormtrooper/animations/dance.ozz";
const char* gDiffuseTexture = "Stormtrooper_D.tex";

//Profiler
ProfileToken   gGpuProfileToken;
uint32_t       gFrameIndex = 0;

//Fonts
FontDrawDesc gFrameTimeDraw;
uint32_t gFontID = 0;

static HiresTimer gAnimationUpdateTimer;
char debugUIText[64] = { 0 };
float dtSave;
const uint32_t gDataBufferCount = 2;
UIComponent* pStandaloneControlsGUIWindow = NULL;
LoadActionsDesc clearLoadAction{};

////////////////////////////////////////////////////////////////////////////////////
//										Shaders									  //
////////////////////////////////////////////////////////////////////////////////////
Shader* pShaderPlane = NULL;
Shader* pShaderSkinning = NULL;
Shader* pShaderQuad = NULL;
Shader* pShaderAngleCompute = NULL;
Shader* pShaderAnimAccelerator = NULL;

////////////////////////////////////////////////////////////////////////////////////
//									DescriptorSet								  //
////////////////////////////////////////////////////////////////////////////////////
DescriptorSet* pDescriptorSet = NULL;
DescriptorSet* pDescriptorSetSkinning[2] = { NULL };
DescriptorSet* pDescriptorQuad = { NULL };
DescriptorSet* pDescriptorSetAnimAccelerator[2] = { NULL };
DescriptorSet* pDescriptorSetCompAngleCompute = NULL;

////////////////////////////////////////////////////////////////////////////////////
//									RootSignatures								  //
////////////////////////////////////////////////////////////////////////////////////
RootSignature* pRootSignaturePlane = NULL;
RootSignature* pRootSignatureSkinning = NULL;
RootSignature* pRootSignatureQuad = NULL;
RootSignature* pRootSigAnimAccelerator = NULL;
RootSignature* pRootSigCompAngleCompute = NULL;

////////////////////////////////////////////////////////////////////////////////////
//									Pipeline									  //
////////////////////////////////////////////////////////////////////////////////////
Pipeline* pPlaneDrawPipeline = NULL;
Pipeline* pPipelineSkinning = NULL;
Pipeline* pPipelineQuad = NULL;
Pipeline* pPipelineAnimAccelerator = NULL;
Pipeline* pPipelineCompAngleCompute = NULL;

////////////////////////////////////////////////////////////////////////////////////
//									Buffers										  //
////////////////////////////////////////////////////////////////////////////////////
//static
MyBuffer* pBufferJointParentsIndex = NULL;
MyBuffer* pBufferQuadDirection = NULL;
MyBuffer* pBufferPlaneVertex = NULL;
MyBuffer* pBufferQuadVertex = NULL;
MyBuffer* pBufferQuadsPosition = NULL;

//Dynamic
//Transformation Matrices Buffers.
MyBuffer* pBufferPlaneTransformations[2] = { NULL };
MyBuffer* pBufferBoneTransformations[2] = { NULL };
MyBuffer* pBufferQuadTransformations[2] = { NULL };

//Anim Accel calc resources.
MyBuffer* pBufferJointWorldMats[2] = { NULL };
MyBuffer* pBufferJointModelMats[2] = { NULL };
MyBuffer* pBufferJointScales[2] = { NULL };
MyBuffer* pBufferBoneWorldMats[2] = { NULL };

//
MyBuffer* pBufferQuadAngles[2] = { NULL };
MyBuffer* pBufferShadowTransformations = {NULL};
MyBuffer* pBufferFrustumPlanes = {NULL};

////////////////////////////////////////////////////////////////////////////////////
//									Datas										  //
////////////////////////////////////////////////////////////////////////////////////

//Matrices for capturing.
mat4 imposterProjMatrix;
mat4 imposterViewMatrix;
mat4 imposterRotationMatrices[TextureCount];

//Matrix for shadowing (light's point of view).
mat4 lightProjMat;

//Block for passing to "pBufferShadowTransformations" buffer.
MatrixBlock lightMatrixBlock;

//Main camera matrix.
CameraMatrix viewProjMatMainCamera;

vec4 frustumPlanes[6];
int imposterCount = 10000;

////////////////////////////////////////////////////////////////////////////////////
//									Cameras										  //
////////////////////////////////////////////////////////////////////////////////////
ICameraController* mainCamera = NULL;
ICameraController* secondCamera = NULL;
ICameraController* billboardCamera = NULL;
ICameraController* light = NULL;

////////////////////////////////////////////////////////////////////////////////////
//									RTVs										  //
////////////////////////////////////////////////////////////////////////////////////

//For capturing.
RenderTarget* rts[TextureCount] = { NULL };
RenderTarget* pDepthBuffer = NULL;

//For passing to shader.
Texture* rtTextures[TextureCount] = { NULL };

//For shadow rendering.
RenderTarget* shadowRT = NULL;
RenderTarget* shadowDepthRT = NULL;


////////////////////////////////////////////////////////////////////////////////////
//									Animations									  //
////////////////////////////////////////////////////////////////////////////////////
AnimatedObject* gStickFigureAnimObject = NULL;
Animation* gAnimation = NULL;
ClipController* gClipController = NULL;
Clip* gClip = NULL;
Rig* gStickFigureRig = NULL;

//--------------------------------------------------------------------------------------------
// UI DATA
//--------------------------------------------------------------------------------------------
struct UIData
{
	struct ClipData
	{
		bool*  mPlay;
		bool*  mLoop;
		float  mAnimationTime;    // will get set by clip controller
		float* mPlaybackSpeed;
		float3 mOrthographicShadowRangeMin = float3(-20.f, -16.f, -1.f);
		float3 mOrthographicShadowRangeMax = float3(20.f, 20.f, 39.f);
	};
	ClipData mClip;

	struct GeneralSettingsData
	{
		bool mShowBindPose = false;
		bool mDrawShadows = false;
		bool mShowQuads = false;
		bool mOptimizeAnimSim = true;
		bool mFrustumOn = true;
		bool mUsingMainCam = true;
		bool mUsing360Imposter = false;
		int imposterCount = 10000;
	};
	GeneralSettingsData mGeneralSettings;
};
UIData gUIData;

//--------------------------------------------------------------------------------------------
// CALLBACK FUNCTIONS
//--------------------------------------------------------------------------------------------
void ClipTimeChangeCallback(void* pUserData) 
{ 
	gClipController->SetTimeRatioHard(gUIData.mClip.mAnimationTime); 
}

void ResetLightCallback(void* userData)
{
	//Change current light's position to main camera's position, also rotation.
	light->moveTo(mainCamera->getViewPosition());
	light->setViewRotationXY(mainCamera->getRotationXY());
	mat4 lightView = light->getViewMatrix();

	float3 minRange = gUIData.mClip.mOrthographicShadowRangeMin;
	float3 maxRange = gUIData.mClip.mOrthographicShadowRangeMax;
	mat4 orthoProjMat = mat4::orthographicLH(minRange.getX(), maxRange.getX(), minRange.getY(), maxRange.getY(), minRange.getZ(), maxRange.getZ());
	
	lightMatrixBlock.mProjMat = orthoProjMat;
	lightMatrixBlock.mViewMat = lightView;
	lightProjMat = lightMatrixBlock.mProjMat * lightMatrixBlock.mViewMat;

	pBufferShadowTransformations->UpdateData(&lightMatrixBlock);
}


void ResetImposterCountCallback(void* userData)
{
	imposterCount = gUIData.mGeneralSettings.imposterCount;
}

//--------------------------------------------------------------------------------------------
// APP CODE
//--------------------------------------------------------------------------------------------
class ImposterRendering: public IApp
{
	public:
	bool Init()
	{
		initHiresTimer(&gAnimationUpdateTimer);
        // FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG,      "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES,        "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_MESHES,          "Meshes");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS,           "Fonts");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_ANIMATIONS,      "Animation");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS,		   "Scripts");

		//Init all resources, setups.
		Initialize();
		
		// PROFILER SETUP
		gGpuProfileToken = addGpuProfiler(renderer, queue, "Graphics");

		// UI SETUP
		vec2    UIPosition = { mSettings.mWidth * 0.01f, mSettings.mHeight * 0.15f };
		vec2    UIPanelSize = { 650, 1000 };
		UIComponentDesc guiDesc;
		guiDesc.mStartPosition = UIPosition;
		guiDesc.mStartSize = UIPanelSize;
		guiDesc.mFontID = 0;
		guiDesc.mFontSize = 16.0f;
		uiCreateComponent("Animation", &guiDesc, &pStandaloneControlsGUIWindow);

		gUIData.mClip.mPlay = &gClipController->mPlay;
		gUIData.mClip.mLoop = &gClipController->mLoop;
		gUIData.mClip.mPlaybackSpeed = &gClipController->mPlaybackSpeed;

		// Initialize UI
		{
			enum
			{
				CLIP_PARAM_SEPARATOR_0,
				CLIP_PARAM_PLAY,
				CLIP_PARAM_SEPARATOR_1,
				CLIP_PARAM_LOOP,
				CLIP_PARAM_SEPARATOR_2,
				CLIP_PARAM_ANIMATION_TIME,
				CLIP_PARAM_SEPARATOR_3,
				CLIP_PARAM_PLAYBACK_SPEED,
				CLIP_PARAM_SEPARATOR_4,
				CLIP_PARAM_SHADOW_RANGE_MIN,
				CLIP_PARAM_SEPARATOR_5,
				CLIP_PARAM_SHADOW_RANGE_MAX,
				CLIP_PARAM_SEPARATOR_6,

				CLIP_PARAM_COUNT
			};

			enum
			{
				GENERAL_PARAM_SEPARATOR_0,
				GENERAL_PARAM_SHOW_BIND_POS,
				GENERAL_PARAM_SEPARATOR_1,
				GENERAL_PARAM_DRAW_SHADOWS,
				GENERAL_PARAM_SEPARATOR_2,
				GENERAL_PARAM_SHOW_QUADS,
				GENERAL_PARAM_SEPARATOR_3,
				GENERAL_PARAM_RESET_CAMERA,
				GENERAL_PARAM_SEPARATOR_4,
				GENERAL_PARAM_FRUSTUM_ON_OFF,
				GENERAL_PARAM_SEPARATOR_5,
				GENERAL_PARAM_OPTIMIZE_ANIM_SIM,
				GENERAL_PARAM_SEPARATOR_6,
				GENERAL_PARAM_USING_MAIN_CAMERA,
				GENERAL_PARAM_SEPARATOR_7,
				GENERAL_PARAM_360_IMPOSTER,
				GENERAL_PARAM_SEPARATOR_8,
				GENERAL_PARAM_IMPOSTER_COUNT,
				GENERAL_PARAM_SEPARATOR_9,
				GENERAL_PARAM_IMPOSTER_COUNT_RESET,
				GENERAL_PARAM_SEPARATOR_10,

				GENERAL_PARAM_COUNT
			};

			static const uint32_t maxWidgetCount = max((uint32_t)CLIP_PARAM_COUNT, (uint32_t)GENERAL_PARAM_COUNT);

			UIWidget widgetBases[maxWidgetCount] = {};
			UIWidget* widgets[maxWidgetCount];
			for (uint32_t i = 0; i < maxWidgetCount; ++i)
				widgets[i] = &widgetBases[i];

			// Set all separators
			SeparatorWidget separator;
			for (uint32_t i = 0; i < maxWidgetCount; i += 2)
			{
				widgets[i]->mType = WIDGET_TYPE_SEPARATOR;
				widgets[i]->mLabel[0] = '\0';
				widgets[i]->pWidget = &separator;
			}

			// STAND CLIP
			//
			CollapsingHeaderWidget collapsingClipWidgets;
			collapsingClipWidgets.pGroupedWidgets = widgets;
			collapsingClipWidgets.mWidgetsCount = CLIP_PARAM_COUNT;

			// Play/Pause - Checkbox
			CheckboxWidget playCheckbox = {};
			playCheckbox.pData = gUIData.mClip.mPlay;
			widgets[CLIP_PARAM_PLAY]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[CLIP_PARAM_PLAY]->mLabel, "Play");
			widgets[CLIP_PARAM_PLAY]->pWidget = &playCheckbox;

			// Loop - Checkbox
			CheckboxWidget loopCheckbox = {};
			loopCheckbox.pData = gUIData.mClip.mLoop;
			widgets[CLIP_PARAM_LOOP]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[CLIP_PARAM_LOOP]->mLabel, "Loop");
			widgets[CLIP_PARAM_LOOP]->pWidget = &loopCheckbox;

			// Animation Time - Slider
			float             fValMin = 0.0f;
			float             fValMax = gClipController->mDuration;
			float             sliderStepSize = 0.01f;

			SliderFloatWidget animationTime;
			animationTime.pData = &gUIData.mClip.mAnimationTime;
			animationTime.mMin = fValMin;
			animationTime.mMax = fValMax;
			animationTime.mStep = sliderStepSize;
			widgets[CLIP_PARAM_ANIMATION_TIME]->mType = WIDGET_TYPE_SLIDER_FLOAT;
			strcpy(widgets[CLIP_PARAM_ANIMATION_TIME]->mLabel, "Animation Time");
			widgets[CLIP_PARAM_ANIMATION_TIME]->pWidget = &animationTime;
			uiSetWidgetOnActiveCallback(widgets[CLIP_PARAM_ANIMATION_TIME], nullptr, ClipTimeChangeCallback);

			// Playback Speed - Slider
			fValMin = -5.0f;
			fValMax = 5.0f;
			sliderStepSize = 0.1f;

			SliderFloatWidget playbackSpeed;
			playbackSpeed.pData = gUIData.mClip.mPlaybackSpeed;
			playbackSpeed.mMin = fValMin;
			playbackSpeed.mMax = fValMax;
			playbackSpeed.mStep = sliderStepSize;
			widgets[CLIP_PARAM_PLAYBACK_SPEED]->mType = WIDGET_TYPE_SLIDER_FLOAT;
			strcpy(widgets[CLIP_PARAM_PLAYBACK_SPEED]->mLabel, "Playback Speed");
			widgets[CLIP_PARAM_PLAYBACK_SPEED]->pWidget = &playbackSpeed;

			float3 min(-40.f, -40.f, -40.f);
			float3 max(-.1f, -.1f, -.1f);
			float3 stepSize(1.f, 1.f, 1.f);

			SliderFloat3Widget shadowRangeMin;
			shadowRangeMin.pData = &gUIData.mClip.mOrthographicShadowRangeMin;
			shadowRangeMin.mMin = min;
			shadowRangeMin.mMax = max;
			shadowRangeMin.mStep = stepSize;
			widgets[CLIP_PARAM_SHADOW_RANGE_MIN]->mType = WIDGET_TYPE_SLIDER_FLOAT3;
			strcpy(widgets[CLIP_PARAM_SHADOW_RANGE_MIN]->mLabel, "Shadow Range Min");
			widgets[CLIP_PARAM_SHADOW_RANGE_MIN]->pWidget = &shadowRangeMin;

			min = float3(0.1f, 0.1f, 0.1f);
			max = float3(40.f, 40.f, 40.f);
			stepSize = float3(1.f, 1.f, 1.f);

			SliderFloat3Widget shadowRangeMax;
			shadowRangeMax.pData = &gUIData.mClip.mOrthographicShadowRangeMax;
			shadowRangeMax.mMin = min;
			shadowRangeMax.mMax = max;
			shadowRangeMax.mStep = stepSize;
			widgets[CLIP_PARAM_SHADOW_RANGE_MAX]->mType = WIDGET_TYPE_SLIDER_FLOAT3;
			strcpy(widgets[CLIP_PARAM_SHADOW_RANGE_MAX]->mLabel, "Shadow Range Max");
			widgets[CLIP_PARAM_SHADOW_RANGE_MAX]->pWidget = &shadowRangeMax;


			luaRegisterWidget(uiCreateComponentWidget(pStandaloneControlsGUIWindow, "Clip", &collapsingClipWidgets, WIDGET_TYPE_COLLAPSING_HEADER));

			// GENERAL SETTINGS
			//
			CollapsingHeaderWidget collapsingGeneralSettingsWidgets;
			collapsingGeneralSettingsWidgets.pGroupedWidgets = widgets;
			collapsingGeneralSettingsWidgets.mWidgetsCount = GENERAL_PARAM_COUNT;

			// ShowBindPose - Checkbox
			CheckboxWidget showBindPose;
			showBindPose.pData = &gUIData.mGeneralSettings.mShowBindPose;
			widgets[GENERAL_PARAM_SHOW_BIND_POS]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_SHOW_BIND_POS]->mLabel, "Show Bind Pose");
			widgets[GENERAL_PARAM_SHOW_BIND_POS]->pWidget = &showBindPose;

			// DrawShadows - Checkbox
			CheckboxWidget drawShadows;
			drawShadows.pData = &gUIData.mGeneralSettings.mDrawShadows;
			widgets[GENERAL_PARAM_DRAW_SHADOWS]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_DRAW_SHADOWS]->mLabel, "Draw Shadows");
			widgets[GENERAL_PARAM_DRAW_SHADOWS]->pWidget = &drawShadows;
			uiSetWidgetOnActiveCallback(widgets[GENERAL_PARAM_DRAW_SHADOWS], nullptr, ResetLightCallback);

			CheckboxWidget drawQuads;
			drawQuads.pData = &gUIData.mGeneralSettings.mShowQuads;
			widgets[GENERAL_PARAM_SHOW_QUADS]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_SHOW_QUADS]->mLabel, "Draw Quads");
			widgets[GENERAL_PARAM_SHOW_QUADS]->pWidget = &drawQuads;

			CheckboxWidget resetCamera;
			resetCamera.pData = NULL;
			widgets[GENERAL_PARAM_RESET_CAMERA]->mType = WIDGET_TYPE_BUTTON;
			strcpy(widgets[GENERAL_PARAM_RESET_CAMERA]->mLabel, "Reset Shadow");
			widgets[GENERAL_PARAM_RESET_CAMERA]->pWidget = &resetCamera;
			uiSetWidgetOnActiveCallback(widgets[GENERAL_PARAM_RESET_CAMERA], nullptr, ResetLightCallback);

			CheckboxWidget frustumOn;
			frustumOn.pData = &gUIData.mGeneralSettings.mFrustumOn;
			widgets[GENERAL_PARAM_FRUSTUM_ON_OFF]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_FRUSTUM_ON_OFF]->mLabel, "Frustum On");
			widgets[GENERAL_PARAM_FRUSTUM_ON_OFF]->pWidget = &frustumOn;

			CheckboxWidget optimizeAnim;
			optimizeAnim.pData = &gUIData.mGeneralSettings.mOptimizeAnimSim;
			widgets[GENERAL_PARAM_OPTIMIZE_ANIM_SIM]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_OPTIMIZE_ANIM_SIM]->mLabel, "Optimize Anim Sims");
			widgets[GENERAL_PARAM_OPTIMIZE_ANIM_SIM]->pWidget = &optimizeAnim;

			CheckboxWidget usingMainCamera;
			usingMainCamera.pData = &gUIData.mGeneralSettings.mUsingMainCam;
			widgets[GENERAL_PARAM_USING_MAIN_CAMERA]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_USING_MAIN_CAMERA]->mLabel, "Using MainCamera");
			widgets[GENERAL_PARAM_USING_MAIN_CAMERA]->pWidget = &usingMainCamera;

			CheckboxWidget using360Imposter;
			using360Imposter.pData = &gUIData.mGeneralSettings.mUsing360Imposter;
			widgets[GENERAL_PARAM_360_IMPOSTER]->mType = WIDGET_TYPE_CHECKBOX;
			strcpy(widgets[GENERAL_PARAM_360_IMPOSTER]->mLabel, "360 Imposter");
			widgets[GENERAL_PARAM_360_IMPOSTER]->pWidget = &using360Imposter;

			SliderIntWidget imposterCount;
			imposterCount.pData = &gUIData.mGeneralSettings.imposterCount;
			imposterCount.mMin = 10000;
			imposterCount.mMax = 200000;
			imposterCount.mStep = 100;
			
			widgets[GENERAL_PARAM_IMPOSTER_COUNT]->mType = WIDGET_TYPE_SLIDER_INT;
			strcpy(widgets[GENERAL_PARAM_IMPOSTER_COUNT]->mLabel, "Imposter Count");
			widgets[GENERAL_PARAM_IMPOSTER_COUNT]->pWidget = &imposterCount;

			CheckboxWidget resetImposter;
			resetImposter.pData = NULL;
			widgets[GENERAL_PARAM_IMPOSTER_COUNT_RESET]->mType = WIDGET_TYPE_BUTTON;
			strcpy(widgets[GENERAL_PARAM_IMPOSTER_COUNT_RESET]->mLabel, "Reset ImposterCount");
			widgets[GENERAL_PARAM_IMPOSTER_COUNT_RESET]->pWidget = &resetImposter;
			uiSetWidgetOnActiveCallback(widgets[GENERAL_PARAM_IMPOSTER_COUNT_RESET], nullptr, ResetImposterCountCallback);

			luaRegisterWidget(uiCreateComponentWidget(pStandaloneControlsGUIWindow, "General Settings", &collapsingGeneralSettingsWidgets, WIDGET_TYPE_COLLAPSING_HEADER));
		}

		waitForAllResourceLoads();

		InputSystemDesc inputDesc = {};
		inputDesc.pRenderer = renderer;
		inputDesc.pWindow = pWindow;
		if (!initInputSystem(&inputDesc))
			return false;

		// App Actions
		{
			InputActionDesc actionDesc =
			{
				DefaultInputActions::DUMP_PROFILE_DATA, [](InputActionContext* ctx) {  dumpProfileData(((Renderer*)ctx->pUserData)->pName); return true; }, renderer
			};
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::TOGGLE_FULLSCREEN, [](InputActionContext* ctx) {
				WindowDesc* winDesc = ((IApp*)ctx->pUserData)->pWindow;
				if (winDesc->fullScreen)
					winDesc->borderlessWindow ? setBorderless(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect)) : setWindowed(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect));
				else
					setFullscreen(winDesc);
				return true;
			}, this };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
			addInputAction(&actionDesc);
			InputActionCallback onUIInput = [](InputActionContext* ctx)
			{
				if (ctx->mActionId > UISystemInputActions::UI_ACTION_START_ID_)
				{
					uiOnInput(ctx->mActionId, ctx->mBool, ctx->pPosition, &ctx->mFloat2);
				}
				return true;
			};

			typedef bool(*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
			static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
			{
				if (*(ctx->pCaptured))
				{
					if (gUIData.mGeneralSettings.mUsingMainCam)
					{
						float2 delta = uiIsFocused() ? float2(0.f, 0.f) : ctx->mFloat2;
						index ? mainCamera->onRotate(delta) : mainCamera->onMove(delta);
					}
					else
					{
						float2 delta = uiIsFocused() ? float2(0.f, 0.f) : ctx->mFloat2;
						index ? secondCamera->onRotate(delta) : secondCamera->onMove(delta);
					}

				}
				return true;
			};
			actionDesc = { DefaultInputActions::CAPTURE_INPUT, [](InputActionContext* ctx) {setEnableCaptureInput(!uiIsFocused() && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);	return true; }, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::ROTATE_CAMERA, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::TRANSLATE_CAMERA, [](InputActionContext* ctx)
			{
				return onCameraInput(ctx, 0);
			}, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::RESET_CAMERA, [](InputActionContext* ctx) { if (!uiWantTextInput()) mainCamera->resetView(); return true; } };
			addInputAction(&actionDesc);
			GlobalInputActionDesc globalInputActionDesc = { GlobalInputActionDesc::ANY_BUTTON_ACTION, onUIInput, this };
			setGlobalInputAction(&globalInputActionDesc);
		}

		gFrameIndex = 0;

		return true;
	}

	void Exit()
	{
		//Exit Systems.
		exitInputSystem();
		exitProfiler();
		exitUserInterface();
		exitFontSystem();

		//Exit all resources.
		removeResource(pGeomData);
		pGeomData = nullptr;
		removeResource(pGeom);
		pGeom = nullptr;

		//Remove all buffer resources.
		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			removeResource(pBufferBoneTransformations[i]->buffer);
			removeResource(pBufferQuadTransformations[i]->buffer);
			removeResource(pBufferJointModelMats[i]->buffer);
			removeResource(pBufferJointWorldMats[i]->buffer);
			removeResource(pBufferJointScales[i]->buffer);
			removeResource(pBufferBoneWorldMats[i]->buffer);
			removeResource(pBufferPlaneTransformations[i]->buffer);
			removeResource(pBufferQuadAngles[i]->buffer);
			
			tf_free(pBufferBoneTransformations[i]);
			tf_free(pBufferQuadTransformations[i]);
			tf_free(pBufferJointModelMats[i]);
			tf_free(pBufferJointWorldMats[i]);
			tf_free(pBufferJointScales[i]);
			tf_free(pBufferBoneWorldMats[i]);
			tf_free(pBufferPlaneTransformations[i]);
			tf_free(pBufferQuadAngles[i]);
		}
		removeResource(pTextureDiffuse);

		removeResource(pBufferPlaneVertex->buffer);
		tf_free(pBufferPlaneVertex);

		removeResource(pBufferJointParentsIndex->buffer);
		tf_free(pBufferJointParentsIndex);

		removeResource(pBufferQuadVertex->buffer);
		tf_free(pBufferQuadVertex);

		removeResource(pBufferQuadDirection->buffer);
		tf_free(pBufferQuadDirection);

		removeResource(pBufferQuadsPosition->buffer);
		tf_free(pBufferQuadsPosition);

		removeResource(pBufferShadowTransformations->buffer);
		tf_free(pBufferShadowTransformations);

		removeResource(pBufferFrustumPlanes->buffer);
		tf_free(pBufferFrustumPlanes);

		//Exit camera controllers.
		exitCameraController(billboardCamera);
		exitCameraController(light);
		exitCameraController(mainCamera);
		exitCameraController(secondCamera);

		//Exit samplers.
		removeSampler(renderer, pDefaultSampler);
		removeSampler(renderer, pShadowSampler);

		removeSemaphore(renderer, pImageAcquiredSemaphore);
		removeGpuCmdRing(renderer, gGraphicsCmdRing);

		exitResourceLoaderInterface(renderer);
		removeQueue(renderer, queue);

		tf_delete(gGraphicsCmdRing);
		exitRenderer(renderer);
		renderer = NULL;

		//Exit all animation datas.
		gStickFigureRig->Exit();
		gClip->Exit();
		gAnimation->Exit();
		gStickFigureAnimObject->Exit();

		tf_delete(gStickFigureRig);
		tf_delete(gClip);
		tf_delete(gClipController);
		tf_delete(gStickFigureAnimObject);
		tf_delete(gAnimation);
	}

	bool Load(ReloadDesc* pReloadDesc)
	{
		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			//Add rendertargets.
			if (!AddSwapChain())
				return false;

			AddRenderTargets();
		}

		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			//Add Shaders.
			AddShaders();
			AddRootSignatures();
			AddDescriptorSets();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			//Add pipelines.
			AddPipelines();
		}

		//Prepare descritpor settings.
		PrepareDescriptorSets();

		UserInterfaceLoadDesc uiLoad = {};
		uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		uiLoad.mHeight = mSettings.mHeight;
		uiLoad.mWidth = mSettings.mWidth;
		uiLoad.mLoadType = pReloadDesc->mType;
		loadUserInterface(&uiLoad);

		FontSystemLoadDesc fontLoad = {};
		fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		fontLoad.mHeight = mSettings.mHeight;
		fontLoad.mWidth = mSettings.mWidth;
		fontLoad.mLoadType = pReloadDesc->mType;
		loadFontSystem(&fontLoad);

		return true;
	}

	void Unload(ReloadDesc* pReloadDesc)
	{
		waitQueueIdle(queue);
		unloadFontSystem(pReloadDesc->mType);
		unloadUserInterface(pReloadDesc->mType);

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			//Remove pipelines.
			RemovePipelines();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			//Remove render targets.
			removeSwapChain(renderer, pSwapChain);
			RemoveRenderTargets();
		}

		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			//Remove shader resources.
			RemoveDescriptorSets();
			RemoveRootSignatures();
			RemoveShaders();
		}
	}

	void Update(float deltaTime)
	{
		/************************************************************************/
		// Input Update
		/************************************************************************/

		dtSave = deltaTime;
		updateInputSystem(deltaTime, mSettings.mWidth, mSettings.mHeight);

		if(gUIData.mGeneralSettings.mUsingMainCam)
			mainCamera->update(deltaTime);
		else
			secondCamera->update(deltaTime);

		/************************************************************************/
		// Scene Update
		/************************************************************************/

		mat4 viewMat = mainCamera->getViewMatrix();
		mat4 secondCameraViewMat = secondCamera->getViewMatrix();

		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.0f;

		CameraMatrix projMat = CameraMatrix::perspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 1000.0f);

		/************************************************************************/
		// Animation Update
		/************************************************************************/
		resetHiresTimer(&gAnimationUpdateTimer);

		// Record animation update time
		getHiresTimerUSec(&gAnimationUpdateTimer, true);

		projViewModelMatrices.mProjMat = projMat.getPrimaryMatrix();

		if(gUIData.mGeneralSettings.mUsingMainCam)
			projViewModelMatrices.mViewMat = viewMat;
		else
			projViewModelMatrices.mViewMat = secondCameraViewMat;

		viewProjMatMainCamera = projMat * viewMat;
	}

	void Draw()
	{
		/************************************************************************/
		// Swapchain Settings.
		/************************************************************************/
		if (pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
		{
			waitQueueIdle(queue);
			::toggleVSync(renderer, &pSwapChain);
		}
		
		uint32_t swapchainImageIndex;
		acquireNextImage(renderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		GpuCmdRingElement elem = getNextGpuCmdRingElement(gGraphicsCmdRing, true, 1);
		FenceStatus fenceStatus;
		getFenceStatus(renderer, elem.pFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(renderer, 1, &elem.pFence);

		/************************************************************************/
		// Cmds
		/************************************************************************/

		resetCmdPool(renderer, elem.pCmdPool);

		Cmd* cmd = elem.pCmds[0];
		beginCmd(cmd);    // start recording commands
		
		// start gpu frame profiler
		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		/************************************************************************/
		// Anim Datas
		/************************************************************************/
		UpdateAnims(cmd, &gGpuProfileToken);

		pBufferPlaneTransformations[gFrameIndex]->UpdateData(&projViewModelMatrices);
		pBufferBoneTransformations[gFrameIndex]->UpdateData(&gUniformDataBones);
		pBufferQuadTransformations[gFrameIndex]->UpdateData(&projViewModelMatrices);

		//Angle Compute btw camera & billboards.
		DispatchAngleCompute(cmd);

		RenderTargetBarrier rtsBarrier[TextureCount] = {};
		RenderTargetBarrier shadowDepthBarrier = {};

		//Change rts state to render target for capturing.
		for(int i = 0; i < TextureCount; ++i)
			rtsBarrier[i] = {rts[i], RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TextureCount, rtsBarrier);

		shadowDepthBarrier = {shadowDepthRT, RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_WRITE};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &shadowDepthBarrier);

		//Capture to rendertarget of skinning anims.
		CaptureToRT(cmd);

		//Change rts state to shader resource for using as texture.
		for(int i = 0; i < TextureCount; ++i)
			rtsBarrier[i] = {rts[i], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TextureCount, rtsBarrier);

		//Store depth values of the scene.
		FillShadowDepthRT(cmd);

		shadowDepthBarrier = {shadowDepthRT, RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_SHADER_RESOURCE};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &shadowDepthBarrier);

		//Back to the default render target.
		BindDefaultRT(cmd, swapchainImageIndex);

		/************************************************************************/
		// Render Funcs
		/************************************************************************/
		RenderPlane(cmd);

		RenderQuads(cmd);

		RenderAnimation(cmd);

		/************************************************************************/
		// Render UIs
		/************************************************************************/
		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
		LoadActionsDesc loadActions = {};
		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);

		gFrameTimeDraw.mFontColor = 0xff00ffff;
		gFrameTimeDraw.mFontSize = 18.0f;
		gFrameTimeDraw.mFontID = gFontID;
		float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

		snprintf(debugUIText,  64, "Animation Update %f ms", getHiresTimerUSecAverage(&gAnimationUpdateTimer) / 1000.0f);

		gFrameTimeDraw.pText = debugUIText;
		cmdDrawTextWithFont(cmd, float2(8.f, txtSize.y + 75.f), &gFrameTimeDraw);

		bool isLeft = false;
		int xzAngle = GetXZAngle(isLeft);
		float yAngle = GetYAngle();

		snprintf(debugUIText, 64, "XZ Angle : %d", xzAngle);
		gFrameTimeDraw.pText = debugUIText;
		cmdDrawTextWithFont(cmd, float2(8.f, txtSize.y + 95.f), &gFrameTimeDraw);

		snprintf(debugUIText, 64, "Y Angle : %f", yAngle);
		gFrameTimeDraw.pText = debugUIText;
		cmdDrawTextWithFont(cmd, float2(8.f, txtSize.y + 115.f), &gFrameTimeDraw);

		if(isLeft)
			snprintf(debugUIText, 64, "Left");
		else
			snprintf(debugUIText, 64, "Right");
		
		gFrameTimeDraw.pText = debugUIText;
		cmdDrawTextWithFont(cmd, float2(8.f, txtSize.y + 135.f), &gFrameTimeDraw);

		cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y * 2.f + 160.f), gGpuProfileToken, &gFrameTimeDraw);

		cmdDrawUserInterface(cmd);

		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndDebugMarker(cmd);


		// PRESENT THE GRPAHICS QUEUE
		RenderTargetBarrier barrier = {pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &elem.pSemaphore;
		submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
		submitDesc.pSignalFence = elem.pFence;
		queueSubmit(queue, &submitDesc);
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.ppWaitSemaphores = &elem.pSemaphore;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.mSubmitDone = true;
		queuePresent(queue, &presentDesc);
		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gDataBufferCount;
	}

	////////////////////////////////////////////////////////////////////////////////////
	//										Init Funcs								  //
	////////////////////////////////////////////////////////////////////////////////////
	void Initialize()
	{
		gStickFigureRig = tf_new(Rig);
		gClip = tf_new(Clip);
		gClipController = tf_new(ClipController);
		gStickFigureAnimObject = tf_new(AnimatedObject);
		gAnimation = tf_new(Animation);

		//Initialize the rig with the path to its ozz file.
		gStickFigureRig->Initialize(RD_ANIMATIONS, gStickFigureName);

		//Clip initialize.
		gClip->Initialize(RD_ANIMATIONS, gClipName, gStickFigureRig);

		ASSERT(MAX_NUM_BONES >= gStickFigureRig->mNumJoints);

		//Clip controller initialize.
		//Initialize with the length of the clip they are controlling and an
		//optional external time to set based on their updating.
		gClipController->Initialize(gClip->GetDuration(), &gUIData.mClip.mAnimationTime);

		//Anim initialization.
		AnimationDesc animationDesc{};

		animationDesc.mRig = gStickFigureRig;
		animationDesc.mNumLayers = 1;
		animationDesc.mLayerProperties[0].mClip = gClip;
		animationDesc.mLayerProperties[0].mClipController = gClipController;

		gAnimation->Initialize(animationDesc);
		gStickFigureAnimObject->Initialize(gStickFigureRig, gAnimation);

		//Setting Renderer
		RendererDesc setting = {};
		setting.mD3D11Supported = true;
		initRenderer(GetName(), &setting, &renderer);

		//Setting Queue
		QueueDesc queueDesc{};

		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;

		addQueue(renderer, &queueDesc, &queue);

		//Setting cmdRing
		gGraphicsCmdRing = tf_new(GpuCmdRing);

		GpuCmdRingDesc cmdRingDesc;
		cmdRingDesc.pQueue = queue;
		cmdRingDesc.mPoolCount = gDataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;

		addGpuCmdRing(renderer, &cmdRingDesc, gGraphicsCmdRing);

		//Semaphore
		addSemaphore(renderer, &pImageAcquiredSemaphore);

		initResourceLoaderInterface(renderer);

		//Setting Font
		FontDesc font{};
		font.pFontPath = "TitilliumText/TitilliumText-Bold.otf";

		fntDefineFonts(&font, 1, &gFontID);

		FontSystemDesc fontRenderDesc{};
		fontRenderDesc.pRenderer = renderer;

		if (!initFontSystem(&fontRenderDesc))
			return;

		//User Interface
		UserInterfaceDesc uiRenderDesc{};

		uiRenderDesc.pRenderer = renderer;
		initUserInterface(&uiRenderDesc);

		//Profiler
		ProfilerDesc profiler{};

		profiler.pRenderer = renderer;
		profiler.mWidthUI = mSettings.mWidth;
		profiler.mHeightUI = mSettings.mHeight;

		initProfiler(&profiler);

		//Setting Sampler
		SamplerDesc defaultSamplerDesc{};

		defaultSamplerDesc.mAddressU = ADDRESS_MODE_REPEAT;
		defaultSamplerDesc.mAddressV = ADDRESS_MODE_REPEAT;
		defaultSamplerDesc.mAddressW = ADDRESS_MODE_REPEAT;
		defaultSamplerDesc.mMinFilter = FILTER_LINEAR;
		defaultSamplerDesc.mMagFilter = FILTER_LINEAR;
		defaultSamplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;

		addSampler(renderer, &defaultSamplerDesc, &pDefaultSampler);

		defaultSamplerDesc = {};
		defaultSamplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_BORDER;
		defaultSamplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_BORDER;
		defaultSamplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_BORDER;
		defaultSamplerDesc.mMinFilter = FILTER_LINEAR;
		defaultSamplerDesc.mMagFilter = FILTER_LINEAR;
		defaultSamplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;

		addSampler(renderer, &defaultSamplerDesc, &pShadowSampler);

		//Setting Texture
		TextureLoadDesc diffuseTextureDesc{};

		diffuseTextureDesc.pFileName = gDiffuseTexture;
		diffuseTextureDesc.ppTexture = &pTextureDiffuse;
		diffuseTextureDesc.mCreationFlag = TEXTURE_CREATION_FLAG_SRGB;

		addResource(&diffuseTextureDesc, NULL);

		//Setting shadow proj mat.
		float3 minRange = gUIData.mClip.mOrthographicShadowRangeMin;
		float3 maxRange = gUIData.mClip.mOrthographicShadowRangeMax;
		mat4 orthoProjMat = mat4::orthographicLH(minRange.getX(), maxRange.getX(), minRange.getY(), maxRange.getY(), minRange.getZ(), maxRange.getZ());
		//need to initialize pBufferShadowTransformations. with this otrhoProjMat
		lightMatrixBlock.mProjMat = orthoProjMat;
		projViewModelMatrices.mToWorldMat = mat4::scale(Vector3(110.f, 1.f, 110.f));
		
		//Init clear load action.
		clearLoadAction.mLoadActionDepth = LOAD_ACTION_CLEAR;
		clearLoadAction.mClearDepth = { {0.f, 0.f} };
		clearLoadAction.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		clearLoadAction.mClearColorValues[0] = { 0.f, 0.f, 0.f, 0.f };

		//Buffers
		InitResources();

		//Cameras
		InitCameraControllers();

		//GeomLoad
		InitGeometryLoad();
	}

	void InitGeometryLoad()
	{
		VertexLayout gVertexLayoutSkinned{};
		gVertexLayoutSkinned.mBindingCount = 1;
		gVertexLayoutSkinned.mAttribCount = 5;
		gVertexLayoutSkinned.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		gVertexLayoutSkinned.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[0].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[0].mLocation = 0;
		gVertexLayoutSkinned.mAttribs[0].mOffset = 0;
		gVertexLayoutSkinned.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		gVertexLayoutSkinned.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[1].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[1].mLocation = 1;
		gVertexLayoutSkinned.mAttribs[1].mOffset = 3 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
		gVertexLayoutSkinned.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[2].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[2].mLocation = 2;
		gVertexLayoutSkinned.mAttribs[2].mOffset = 6 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[3].mSemantic = SEMANTIC_WEIGHTS;
		gVertexLayoutSkinned.mAttribs[3].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[3].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[3].mLocation = 3;
		gVertexLayoutSkinned.mAttribs[3].mOffset = 8 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[4].mSemantic = SEMANTIC_JOINTS;
		gVertexLayoutSkinned.mAttribs[4].mFormat = TinyImageFormat_R16G16B16A16_UINT;
		gVertexLayoutSkinned.mAttribs[4].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[4].mLocation = 4;
		gVertexLayoutSkinned.mAttribs[4].mOffset = 12 * sizeof(float);

		GeometryLoadDesc loadDesc{};
		loadDesc.pFileName = "stormtrooper/riggedMesh.bin";
		loadDesc.pVertexLayout = &gVertexLayoutSkinned;
		loadDesc.ppGeometry = &pGeom;
		loadDesc.ppGeometryData = &pGeomData;
		loadDesc.mFlags = GEOMETRY_LOAD_FLAG_SHADOWED;

		addResource(&loadDesc, NULL);
	}

	void InitResources()
	{
		//Populate buffers.
		pBufferQuadVertex =				(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferQuadsPosition =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferQuadDirection =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferPlaneVertex =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferJointParentsIndex =		(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferShadowTransformations = 	(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		pBufferFrustumPlanes = 			(MyBuffer*)tf_malloc(sizeof(MyBuffer));

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			pBufferBoneTransformations[i] =		(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferQuadTransformations[i] =		(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferQuadAngles[i] =				(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferPlaneTransformations[i] =	(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferJointScales[i] =				(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferBoneWorldMats[i] =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferJointModelMats[i] =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
			pBufferJointWorldMats[i] =			(MyBuffer*)tf_malloc(sizeof(MyBuffer));
		}

		InitBoneResource();
		InitQuadResource();
		InitImposterResource();
		InitPlaneResource();
		InitAnimAccelResource();
		InitFrustumResource();
	}

	void InitBoneResource()
	{
		//Setting buffers
		BufferLoadDesc boneUniformBufferDesc{};

		boneUniformBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		boneUniformBufferDesc.mDesc.mElementCount = 128;
		boneUniformBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		boneUniformBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;

		boneUniformBufferDesc.mDesc.mStructStride = sizeof(mat4);
		boneUniformBufferDesc.mDesc.mSize = boneUniformBufferDesc.mDesc.mStructStride * boneUniformBufferDesc.mDesc.mElementCount;
		boneUniformBufferDesc.mDesc.pName = "PlaneUniformBuffer";
		boneUniformBufferDesc.pData = NULL;

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			boneUniformBufferDesc.ppBuffer = &pBufferBoneTransformations[i]->buffer;
			addResource(&boneUniformBufferDesc, NULL);
			pBufferBoneTransformations[i]->size = boneUniformBufferDesc.mDesc.mSize;
		}
	}

	void InitQuadResource()
	{
		//Setting buffers
		float quadPoints[] = { -1.f, 1.f, 1.f, 1.f, 1.f, 0.f,
							  -1.f, -1.f, 1.f, 1.f, 1.f, 1.f,
							  1.f, -1.f, 1.f, 1.f, 0.f, 1.f,

							  1.f, -1.f, 1.f, 1.f, 0.f, 1.f,
							  1.f, 1.f, 1.f, 1.f, 0.f, 0.f,
							  -1.f, 1.f, 1.f, 1.f, 1.f, 0.f };

		BufferLoadDesc quadBufferDesc{};

		quadBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		quadBufferDesc.mDesc.mElementCount = 36;
		quadBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		quadBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;

		quadBufferDesc.mDesc.mStructStride = sizeof(float);
		quadBufferDesc.mDesc.mSize = quadBufferDesc.mDesc.mStructStride * quadBufferDesc.mDesc.mElementCount;
		quadBufferDesc.mDesc.pName = "QuadVertexBuffer";
		quadBufferDesc.ppBuffer = &pBufferQuadVertex->buffer;
		quadBufferDesc.pData = &quadPoints;

		addResource(&quadBufferDesc, NULL);
		pBufferQuadVertex->size = quadBufferDesc.mDesc.mSize;

		quadBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		quadBufferDesc.mDesc.mElementCount = 2;
		quadBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		quadBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		quadBufferDesc.mDesc.mStructStride = sizeof(mat4);
		quadBufferDesc.mDesc.mSize = quadBufferDesc.mDesc.mStructStride * quadBufferDesc.mDesc.mElementCount;

		quadBufferDesc.ppBuffer = &pBufferShadowTransformations->buffer;
		quadBufferDesc.mDesc.pName = "Shadow Buffer";
		quadBufferDesc.pData = NULL;

		addResource(&quadBufferDesc, NULL);
		pBufferShadowTransformations->size = quadBufferDesc.mDesc.mSize;

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			quadBufferDesc.mDesc.pName = "QuadUniformBuffer";
			quadBufferDesc.ppBuffer = &pBufferQuadTransformations[i]->buffer;
			quadBufferDesc.pData = NULL;

			addResource(&quadBufferDesc, NULL);
			pBufferQuadTransformations[i]->size = quadBufferDesc.mDesc.mSize;
		}
	}

	void InitImposterResource()
	{
		//Populate datas
		vec4* impPositions = (vec4*)tf_malloc(MaxImposterCount * sizeof(vec4));
		vec4* impDirections = (vec4*)tf_malloc(MaxImposterCount * sizeof(vec4));
		int* impCameraIndices = (int*)tf_malloc(MaxImposterCount * sizeof(int));

		//Initializing datas.
		for (int i = 0; i < MaxImposterCount; ++i)
		{
			impCameraIndices[i] = 0;
		}

		vec4 position = { -100.f, .9f, -100.f, 1.f };
		vec4 origin = { 0.f, 0.f, 0.f, 1.f };
		int height = 100;
		int width = 100;
		int cd = MaxImposterCount / ImposterCountPerGroup;

		for (int k = 0; k < cd; ++k)
		{
			for (int i = 0; i < height; ++i)
			{
				for (int j = 0; j < width; ++j)
				{
					int index = (k * width * height) + (i * width) + j;
					impPositions[index] = position;
					position += {0.f, 0.f, 2.f, 0.f};
					vec4 dir = normalize(origin - impPositions[index]);

					impDirections[index] = dir;
				}
				position.setZ(-100.f);
				position += {2.f, 0.f, 0.f, 0.f};
			}
			position.setX(-100.f);
			position.setZ(-100.f);
			position += {0.f, 3.5f, 0.f, 0.f};
		}

		//Setting buffers
		BufferLoadDesc imposterBuffersDescriptrion{};
		imposterBuffersDescriptrion.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
		imposterBuffersDescriptrion.mDesc.mElementCount = MaxImposterCount;
		imposterBuffersDescriptrion.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		imposterBuffersDescriptrion.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;

		imposterBuffersDescriptrion.mDesc.mStructStride = sizeof(float4);
		imposterBuffersDescriptrion.mDesc.mSize = imposterBuffersDescriptrion.mDesc.mStructStride * imposterBuffersDescriptrion.mDesc.mElementCount;
		imposterBuffersDescriptrion.mDesc.pName = "ImposterPosition";
		imposterBuffersDescriptrion.ppBuffer = &pBufferQuadsPosition->buffer;
		imposterBuffersDescriptrion.pData = impPositions;

		addResource(&imposterBuffersDescriptrion, NULL);
		pBufferQuadsPosition->size = imposterBuffersDescriptrion.mDesc.mSize;

		tf_delete(impPositions);

		imposterBuffersDescriptrion.mDesc.mStructStride = sizeof(float4);
		imposterBuffersDescriptrion.mDesc.mSize = imposterBuffersDescriptrion.mDesc.mStructStride * imposterBuffersDescriptrion.mDesc.mElementCount;
		imposterBuffersDescriptrion.mDesc.pName = "ImposterDirection";
		imposterBuffersDescriptrion.ppBuffer = &pBufferQuadDirection->buffer;
		imposterBuffersDescriptrion.pData = impDirections;

		addResource(&imposterBuffersDescriptrion, NULL);
		pBufferQuadDirection->size = imposterBuffersDescriptrion.mDesc.mSize;

		tf_delete(impDirections);

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			imposterBuffersDescriptrion.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
			imposterBuffersDescriptrion.mDesc.mStructStride = sizeof(int);
			imposterBuffersDescriptrion.mDesc.mSize = imposterBuffersDescriptrion.mDesc.mStructStride * imposterBuffersDescriptrion.mDesc.mElementCount;
			imposterBuffersDescriptrion.mDesc.pName = "Imposter Angles";
			imposterBuffersDescriptrion.ppBuffer = &pBufferQuadAngles[i]->buffer;
			imposterBuffersDescriptrion.pData = impCameraIndices;

			addResource(&imposterBuffersDescriptrion, NULL);
			pBufferQuadAngles[i]->size = imposterBuffersDescriptrion.mDesc.mSize;
		}

		tf_delete(impCameraIndices);
	}

	void InitPlaneResource()
	{
		//Setting buffer
		float planePoints[] = { -1.0f, -1.3f, -1.0f, 1.0f, 0.0f, 0.0f,
								-1.0f, -1.3f, 1.0f,  1.0f, 1.0f, 0.0f,
								1.0f,  -1.3f, 1.0f,  1.0f, 1.0f, 1.0f,
								1.0f,  -1.3f, 1.0f,  1.0f, 1.0f, 1.0f,
								1.0f,  -1.3f, -1.0f, 1.0f, 0.0f, 1.0f,
								-1.0f, -1.3f, -1.0f, 1.0f, 0.0f, 0.0f };


		BufferLoadDesc planeVertexBufferDesc{};
		planeVertexBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		planeVertexBufferDesc.mDesc.mElementCount = 36;
		planeVertexBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		planeVertexBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
		planeVertexBufferDesc.mDesc.mStructStride = sizeof(float);
		planeVertexBufferDesc.mDesc.mSize = planeVertexBufferDesc.mDesc.mStructStride * planeVertexBufferDesc.mDesc.mElementCount;
		planeVertexBufferDesc.mDesc.pName = "PlaneVertexBuffer";
		planeVertexBufferDesc.ppBuffer = &pBufferPlaneVertex->buffer;
		planeVertexBufferDesc.pData = planePoints;

		addResource(&planeVertexBufferDesc, NULL);
		pBufferPlaneVertex->size = planeVertexBufferDesc.mDesc.mSize;

		//Setting Uniform buffer
		BufferLoadDesc planeUniformBufferDesc{};
		planeUniformBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		planeUniformBufferDesc.mDesc.mElementCount = 1;
		planeUniformBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		planeUniformBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;

		planeUniformBufferDesc.mDesc.mStructStride = sizeof(MatrixBlock);
		planeUniformBufferDesc.mDesc.mSize = planeUniformBufferDesc.mDesc.mStructStride * planeUniformBufferDesc.mDesc.mElementCount;
		planeUniformBufferDesc.mDesc.pName = "PlaneUniformBuffer";
		planeUniformBufferDesc.pData = NULL;

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			planeUniformBufferDesc.ppBuffer = &pBufferPlaneTransformations[i]->buffer;
			addResource(&planeUniformBufferDesc, NULL);
			pBufferPlaneTransformations[i]->size = planeUniformBufferDesc.mDesc.mSize;
		}
	}

	void InitAnimAccelResource()
	{
		BufferLoadDesc aaJointBufferDesc{};
		aaJointBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
		aaJointBufferDesc.mDesc.mElementCount = gStickFigureAnimObject->mRig->mNumJoints;
		aaJointBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
		aaJointBufferDesc.mDesc.mStructStride = sizeof(int);
		aaJointBufferDesc.mDesc.mSize = aaJointBufferDesc.mDesc.mStructStride * aaJointBufferDesc.mDesc.mElementCount;
		aaJointBufferDesc.mDesc.pName = "JointParentsSlots";
		aaJointBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		aaJointBufferDesc.pData = gStickFigureAnimObject->mRig->mSkeleton.joint_parents().begin();
		aaJointBufferDesc.ppBuffer = &pBufferJointParentsIndex->buffer;
		addResource(&aaJointBufferDesc, NULL);
		pBufferJointParentsIndex->size = aaJointBufferDesc.mDesc.mSize;

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			aaJointBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
			aaJointBufferDesc.mDesc.mStructStride = sizeof(Vector3);
			aaJointBufferDesc.mDesc.mSize = aaJointBufferDesc.mDesc.mStructStride * aaJointBufferDesc.mDesc.mElementCount;
			aaJointBufferDesc.mDesc.pName = "JointScales";
			aaJointBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
			aaJointBufferDesc.pData = NULL;
			aaJointBufferDesc.ppBuffer = &pBufferJointScales[i]->buffer;
			addResource(&aaJointBufferDesc, NULL);
			pBufferJointScales[i]->size = aaJointBufferDesc.mDesc.mSize;

			aaJointBufferDesc.mDesc.mStructStride = sizeof(mat4);
			aaJointBufferDesc.mDesc.mSize = aaJointBufferDesc.mDesc.mStructStride * aaJointBufferDesc.mDesc.mElementCount;
			aaJointBufferDesc.mDesc.pName = "BoneWorldMats";
			aaJointBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
			aaJointBufferDesc.pData = NULL;
			aaJointBufferDesc.ppBuffer = &pBufferBoneWorldMats[i]->buffer;
			addResource(&aaJointBufferDesc, NULL);
			pBufferBoneWorldMats[i]->size = aaJointBufferDesc.mDesc.mSize;

			aaJointBufferDesc.mDesc.mStructStride = sizeof(mat4);
			aaJointBufferDesc.mDesc.mSize = aaJointBufferDesc.mDesc.mStructStride * aaJointBufferDesc.mDesc.mElementCount;
			aaJointBufferDesc.mDesc.pName = "JointModelMats";
			aaJointBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
			aaJointBufferDesc.pData = NULL;
			aaJointBufferDesc.ppBuffer = &pBufferJointModelMats[i]->buffer;
			addResource(&aaJointBufferDesc, NULL);
			pBufferJointModelMats[i]->size = aaJointBufferDesc.mDesc.mSize;


			aaJointBufferDesc.mDesc.mStructStride = sizeof(mat4);
			aaJointBufferDesc.mDesc.mSize = aaJointBufferDesc.mDesc.mStructStride * aaJointBufferDesc.mDesc.mElementCount;
			aaJointBufferDesc.mDesc.pName = "JointWorldMats";
			aaJointBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
			aaJointBufferDesc.pData = NULL;
			aaJointBufferDesc.ppBuffer = &pBufferJointWorldMats[i]->buffer;
			addResource(&aaJointBufferDesc, NULL);
			pBufferJointWorldMats[i]->size = aaJointBufferDesc.mDesc.mSize;
		}
	}

	void InitFrustumResource()
	{
		BufferLoadDesc frustumBufferDesc{};
		frustumBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		frustumBufferDesc.mDesc.mElementCount = 6;
		frustumBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		frustumBufferDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_NONE;
		frustumBufferDesc.mDesc.mStructStride = sizeof(float4);
		frustumBufferDesc.mDesc.mSize = frustumBufferDesc.mDesc.mStructStride * frustumBufferDesc.mDesc.mElementCount;
		frustumBufferDesc.mDesc.pName = "Frustum Blocks Buffer";

		frustumBufferDesc.ppBuffer = &pBufferFrustumPlanes->buffer;
		frustumBufferDesc.pData = NULL;
		addResource(&frustumBufferDesc, NULL);
		pBufferFrustumPlanes->size = frustumBufferDesc.mDesc.mSize;
	}

	void InitCameraControllers()
	{
		CameraMotionParameters cmp{ 50.0f, 75.0f, 150.0f, 0.5f, 0.5f };

		vec3 lookAt{ 0.f, 0.f, -1.f };

		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.f;

		const float zNear = 0.1f;
		const float zFar = 50.f;

		CameraMatrix projMat = CameraMatrix::perspectiveReverseZ(horizontal_fov, aspectInverse, zNear, zFar);

		//Camera for billboard capturing.
		billboardCamera = initFpsCameraController({ 0.f, 4.f, 5.3f }, lookAt);
		billboardCamera->setMotionParameters(cmp);
		billboardCamera->setViewRotationXY({ 0.34f, 3.14f });

		mat4 viewMat = billboardCamera->getViewMatrix();
		float rad2 = degToRad(2.f);

		imposterProjMatrix = projMat.getPrimaryMatrix();
		imposterViewMatrix = viewMat;

		//Setting imposter matrices
		for (int i = 0; i < TextureCount; ++i)
		{
			imposterRotationMatrices[i] = mat4::rotationY(rad2 * static_cast<float>(i));
		}

		vec3 camPos{ -3.0f, 3.0f, 5.0f };
		lookAt = { 0.0f, 1.0f, 0.0f };

		mainCamera = initFpsCameraController(camPos, lookAt);
		mainCamera->setMotionParameters(cmp);

		secondCamera = initFpsCameraController(camPos, lookAt);
		secondCamera->setMotionParameters(cmp);

		light = initFpsCameraController({ 0.f, 0.f, 0.f }, {});
	}

	
	////////////////////////////////////////////////////////////////////////////////////
	//										Load Funcs								  //
	////////////////////////////////////////////////////////////////////////////////////
	void AddShaders()
	{
		//Setup shaders.
		ShaderLoadDesc planeShader{};
		planeShader.mStages[0].pFileName = "plane.vert";
		planeShader.mStages[0].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;
		planeShader.mStages[1].pFileName = "plane.frag";
		planeShader.mStages[1].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;

		ShaderLoadDesc skinningShader{};
		skinningShader.mStages[0].pFileName = "skinning.vert";
		skinningShader.mStages[0].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;
		skinningShader.mStages[1].pFileName = "skinning.frag";
		skinningShader.mStages[1].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;

		ShaderLoadDesc quadShader{};
		quadShader.mStages[0].pFileName = "Billboard.vert";
		quadShader.mStages[0].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;
		quadShader.mStages[1].pFileName = "Billboard.frag";
		quadShader.mStages[1].mFlags = SHADER_STAGE_LOAD_FLAG_NONE;

		ShaderLoadDesc angleShaderDesc{};
		angleShaderDesc.mStages[0].pFileName = "BillboardQuadAngleCompute.comp";

		ShaderLoadDesc animAccelShaderDesc{};
		animAccelShaderDesc.mStages[0].pFileName = "AnimationAccelerator.comp";

		addShader(renderer, &planeShader, &pShaderPlane);
		addShader(renderer, &skinningShader, &pShaderSkinning);
		addShader(renderer, &quadShader, &pShaderQuad);
		addShader(renderer, &angleShaderDesc, &pShaderAngleCompute);
		addShader(renderer, &animAccelShaderDesc, &pShaderAnimAccelerator);
	}

	bool AddSwapChain()
	{
		//Setup swapchain.
		SwapChainDesc swapChainDesc{};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &queue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(renderer, &pWindow->handle);
		swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true, true);
		swapChainDesc.mColorClearValue = { {0.15f, 0.15f, 0.15f, 1.f} };
		swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
		::addSwapChain(renderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	void AddDescriptorSets()
	{
		//Setup descriptorSets.
		DescriptorSetDesc
			setDesc = { pRootSignaturePlane, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gDataBufferCount };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSet);

		setDesc = { pRootSignatureSkinning, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSetSkinning[0]);
		setDesc = { pRootSignatureSkinning, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gDataBufferCount };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSetSkinning[1]);

		setDesc = { pRootSignatureQuad, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gDataBufferCount };
		addDescriptorSet(renderer, &setDesc, &pDescriptorQuad);

		setDesc = { pRootSigCompAngleCompute, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, 2 };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSetCompAngleCompute);

		setDesc = { pRootSigAnimAccelerator, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSetAnimAccelerator[0]);
		setDesc = { pRootSigAnimAccelerator, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, 2 };
		addDescriptorSet(renderer, &setDesc, &pDescriptorSetAnimAccelerator[1]);
	}

	void AddRootSignatures()
	{
		//Setup rootsignatures.
		const char* staticSamplers[] = { "DefaultSampler" };

		RootSignatureDesc rootDesc{};
		rootDesc.mShaderCount = 1;
		rootDesc.mStaticSamplerCount = 1;
		rootDesc.ppStaticSamplerNames = staticSamplers;
		rootDesc.ppStaticSamplers = &pShadowSampler;
		rootDesc.ppShaders = &pShaderPlane;
		addRootSignature(renderer, &rootDesc, &pRootSignaturePlane);

		rootDesc.ppShaders = &pShaderSkinning;
		rootDesc.ppStaticSamplers = &pDefaultSampler;
		addRootSignature(renderer, &rootDesc, &pRootSignatureSkinning);

		rootDesc.ppShaders = &pShaderQuad;
		rootDesc.ppStaticSamplers = &pDefaultSampler;
		addRootSignature(renderer, &rootDesc, &pRootSignatureQuad);

		RootSignatureDesc computeRootDesc = { &pShaderAngleCompute, 1 };
		addRootSignature(renderer, &computeRootDesc, &pRootSigCompAngleCompute);
		computeRootDesc = { &pShaderAnimAccelerator, 1 };
		addRootSignature(renderer, &computeRootDesc, &pRootSigAnimAccelerator);
	}

	void AddPipelines()
	{
		//Setup pipelines.
		VertexLayout vertexLayout{};

		vertexLayout.mBindingCount = 1;
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

		RasterizerStateDesc rasterizerStateDesc{};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		RasterizerStateDesc skeletonRasterizerStateDesc{};
		skeletonRasterizerStateDesc.mCullMode = CULL_MODE_FRONT;

		DepthStateDesc depthStateDesc{};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = CMP_GEQUAL;

		PipelineDesc desc{};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
		pipelineSettings.pRootSignature = pRootSignaturePlane;
		pipelineSettings.pVertexLayout = &vertexLayout;

		vertexLayout = {};
		vertexLayout.mBindingCount = 1;
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pShaderProgram = pShaderPlane;
		addPipeline(renderer, &desc, &pPlaneDrawPipeline);

		VertexLayout gVertexLayoutSkinned{};
		gVertexLayoutSkinned.mBindingCount = 1;
		gVertexLayoutSkinned.mAttribCount = 5;
		gVertexLayoutSkinned.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		gVertexLayoutSkinned.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[0].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[0].mLocation = 0;
		gVertexLayoutSkinned.mAttribs[0].mOffset = 0;
		gVertexLayoutSkinned.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		gVertexLayoutSkinned.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[1].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[1].mLocation = 1;
		gVertexLayoutSkinned.mAttribs[1].mOffset = 3 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
		gVertexLayoutSkinned.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[2].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[2].mLocation = 2;
		gVertexLayoutSkinned.mAttribs[2].mOffset = 6 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[3].mSemantic = SEMANTIC_WEIGHTS;
		gVertexLayoutSkinned.mAttribs[3].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		gVertexLayoutSkinned.mAttribs[3].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[3].mLocation = 3;
		gVertexLayoutSkinned.mAttribs[3].mOffset = 8 * sizeof(float);
		gVertexLayoutSkinned.mAttribs[4].mSemantic = SEMANTIC_JOINTS;
		gVertexLayoutSkinned.mAttribs[4].mFormat = TinyImageFormat_R16G16B16A16_UINT;
		gVertexLayoutSkinned.mAttribs[4].mBinding = 0;
		gVertexLayoutSkinned.mAttribs[4].mLocation = 4;
		gVertexLayoutSkinned.mAttribs[4].mOffset = 12 * sizeof(float);

		pipelineSettings = {};
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
		pipelineSettings.pRootSignature = pRootSignatureSkinning;
		pipelineSettings.pShaderProgram = pShaderSkinning;
		pipelineSettings.pVertexLayout = &gVertexLayoutSkinned;
		pipelineSettings.pRasterizerState = &skeletonRasterizerStateDesc;
		addPipeline(renderer, &desc, &pPipelineSkinning);

		vertexLayout = {};
		vertexLayout.mBindingCount = 1;
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

		RasterizerStateDesc quadRasterizerStateDesc{};
		quadRasterizerStateDesc.mCullMode = CULL_MODE_BACK;

		pipelineSettings = {};
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
		pipelineSettings.pRootSignature = pRootSignatureQuad;
		pipelineSettings.pShaderProgram = pShaderQuad;
		pipelineSettings.pVertexLayout = &vertexLayout;
		pipelineSettings.pRasterizerState = &quadRasterizerStateDesc;
		addPipeline(renderer, &desc, &pPipelineQuad);

		PipelineDesc computeDesc = {};
		computeDesc.mType = PIPELINE_TYPE_COMPUTE;
		ComputePipelineDesc& cPipelineSettings = computeDesc.mComputeDesc;

		cPipelineSettings.pShaderProgram = pShaderAngleCompute;
		cPipelineSettings.pRootSignature = pRootSigCompAngleCompute;
		addPipeline(renderer, &computeDesc, &pPipelineCompAngleCompute);
		cPipelineSettings.pShaderProgram = pShaderAnimAccelerator;
		cPipelineSettings.pRootSignature = pRootSigAnimAccelerator;
		addPipeline(renderer, &computeDesc, &pPipelineAnimAccelerator);
	}

	void AddRenderTargets()
	{
		//Add render targets & initialize rtTextures[].
		RenderTargetDesc rtsDescription{};
		rtsDescription.mArraySize = 1;
		rtsDescription.mClearValue = { 0.f, 0.f, 0.f, 0.f };
		rtsDescription.mDepth = 1;
		rtsDescription.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		rtsDescription.mWidth = mSettings.mWidth;
		rtsDescription.mHeight = mSettings.mHeight;
		rtsDescription.mSampleCount = SAMPLE_COUNT_1;
		rtsDescription.mSampleQuality = 0;
		rtsDescription.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
		rtsDescription.mFormat = getRecommendedSwapchainFormat(true, true);
		rtsDescription.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
		rtsDescription.pName = "Render Targets";

		for (int i = 0; i < TextureCount; ++i)
		{
			addRenderTarget(renderer, &rtsDescription, &rts[i]);
			rtTextures[i] = rts[i]->pTexture;
		}

		//Add Shadow RT.
		rtsDescription.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		rtsDescription.mStartState = RESOURCE_STATE_RENDER_TARGET;
		rtsDescription.pName = "Shadow Render Target";
		addRenderTarget(renderer, &rtsDescription, &shadowRT);

		//Add Shadow Depth RT.
		RenderTargetDesc depthRT{};
		depthRT.mArraySize = 1;
		depthRT.mClearValue.depth = 0.f;
		depthRT.mClearValue.stencil = 0;
		depthRT.mDepth = 1;
		depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
		depthRT.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
		depthRT.mHeight = mSettings.mHeight;
		depthRT.mWidth = mSettings.mWidth;
		depthRT.mSampleCount = SAMPLE_COUNT_1;
		depthRT.mSampleQuality = 0;
		depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE | TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
		addRenderTarget(renderer, &depthRT, &shadowDepthRT);

		//Add depth.
		depthRT.mStartState = RESOURCE_STATE_DEPTH_WRITE;
		addRenderTarget(renderer, &depthRT, &pDepthBuffer);
	}


	void PrepareDescriptorSets()
	{
		//Prepare descriptor setups.
		DescriptorData params[6] = {};
		params[0].pName = "DiffuseTexture";
		params[0].ppTextures = &pTextureDiffuse;

		updateDescriptorSet(renderer, 0, pDescriptorSetSkinning[0], 1, params);

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			params[0] = {};
			params[0].pName = "transformBlock";
			params[0].ppBuffers = &pBufferPlaneTransformations[i]->buffer;

			params[1] = {};
			params[1].pName = "DepthMap";
			params[1].ppTextures = &shadowDepthRT->pTexture;

			updateDescriptorSet(renderer, i, pDescriptorSet, 2, params);
		}

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			params[0] = {};
			params[0].pName = "boneMatrices";
			params[0].ppBuffers = &pBufferBoneTransformations[i]->buffer;
			updateDescriptorSet(renderer, i, pDescriptorSetSkinning[1], 1, params);
		}

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			params[0] = {};
			params[0].pName = "transformBlock";
			params[0].ppBuffers = &pBufferQuadTransformations[i]->buffer;

			params[1] = {};
			params[1].pName = "billboardPositions";
			params[1].ppBuffers = &pBufferQuadsPosition->buffer;

			params[2] = {};
			params[2].pName = "billboardAngles";
			params[2].ppBuffers = &pBufferQuadAngles[i]->buffer;

			params[3] = {};
			params[3].pName = "textures";
			params[3].ppTextures = rtTextures;
			params[3].mCount = TextureCount;

			params[4] = {};
			params[4].pName = "shadowMatBlock";
			params[4].ppBuffers = &pBufferShadowTransformations->buffer;

			updateDescriptorSet(renderer, i, pDescriptorQuad, 5, params);
		}

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			params[0] = {};
			params[0].pName = "billboardPositions";
			params[0].ppBuffers = &pBufferQuadsPosition->buffer;

			params[1] = {};
			params[1].pName = "billboardAngles";
			params[1].ppBuffers = &pBufferQuadAngles[i]->buffer;

			params[2] = {};
			params[2].pName = "billboardDirections";
			params[2].ppBuffers = &pBufferQuadDirection->buffer;

			//Frustum block
			params[3] = {};
			params[3].pName = "frustumBlock";
			params[3].ppBuffers = &pBufferFrustumPlanes->buffer;

			updateDescriptorSet(renderer, i, pDescriptorSetCompAngleCompute, 4, params);
		}

		params[0] = {};
		params[0].pName = "jointParentSlots";
		params[0].ppBuffers = &pBufferJointParentsIndex->buffer;
		updateDescriptorSet(renderer, 0, pDescriptorSetAnimAccelerator[0], 1, params);

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			params[0] = {};
			params[0].pName = "jointWorldMats";
			params[0].ppBuffers = &pBufferJointWorldMats[i]->buffer;
			params[1] = {};
			params[1].pName = "jointModelMats";
			params[1].ppBuffers = &pBufferJointModelMats[i]->buffer;
			params[2] = {};
			params[2].pName = "jointScales";
			params[2].ppBuffers = &pBufferJointScales[i]->buffer;
			params[3] = {};
			params[3].pName = "boneWorldMats";
			params[3].ppBuffers = &pBufferBoneWorldMats[i]->buffer;

			updateDescriptorSet(renderer, i, pDescriptorSetAnimAccelerator[1], 4, params);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////
	//										UnLoad Funcs							  //
	////////////////////////////////////////////////////////////////////////////////////
	void RemoveShaders()
	{
		//Remove shaders.
		removeShader(renderer, pShaderSkinning);
		removeShader(renderer, pShaderPlane);
		removeShader(renderer, pShaderQuad);
		removeShader(renderer, pShaderAngleCompute);
		removeShader(renderer, pShaderAnimAccelerator);
	}

	void RemoveDescriptorSets()
	{
		//Remove descriptorSets.
		removeDescriptorSet(renderer, pDescriptorSet);
		removeDescriptorSet(renderer, pDescriptorSetSkinning[0]);
		removeDescriptorSet(renderer, pDescriptorSetSkinning[1]);
		removeDescriptorSet(renderer, pDescriptorQuad);
		removeDescriptorSet(renderer, pDescriptorSetCompAngleCompute);
		removeDescriptorSet(renderer, pDescriptorSetAnimAccelerator[0]);
		removeDescriptorSet(renderer, pDescriptorSetAnimAccelerator[1]);
	}

	void RemoveRootSignatures()
	{
		//Remove rootSignatures.
		removeRootSignature(renderer, pRootSignatureSkinning);
		removeRootSignature(renderer, pRootSignaturePlane);
		removeRootSignature(renderer, pRootSignatureQuad);
		removeRootSignature(renderer, pRootSigCompAngleCompute);
		removeRootSignature(renderer, pRootSigAnimAccelerator);
	}

	void RemovePipelines()
	{
		//Remove pipelines.
		removePipeline(renderer, pPipelineSkinning);
		removePipeline(renderer, pPlaneDrawPipeline);
		removePipeline(renderer, pPipelineQuad);
		removePipeline(renderer, pPipelineCompAngleCompute);
		removePipeline(renderer, pPipelineAnimAccelerator);
	}

	void RemoveRenderTargets()
	{
		//Remove rendertargets.
		for (int i = 0; i < TextureCount; ++i)
			removeRenderTarget(renderer, rts[i]);

		removeRenderTarget(renderer, shadowDepthRT);
		removeRenderTarget(renderer, shadowRT);
		removeRenderTarget(renderer, pDepthBuffer);
	}

	////////////////////////////////////////////////////////////////////////////////////
	//									ComputeShaders Funcs						  //
	////////////////////////////////////////////////////////////////////////////////////
	void DispatchAngleCompute(Cmd* cmd)
	{
		//Angle computing dispatch.
		cmdBeginGpuTimestampQuery(cmd, NULL, "Angle Comp Dispatch Start");
		uint32_t billboardConstantIndex = getDescriptorIndexFromName(pRootSigCompAngleCompute, "billboardsRootConstant");

		vec3 camPos = mainCamera->getViewPosition();
		vec3 lightPos = light->getViewPosition();

		if (gUIData.mGeneralSettings.mFrustumOn)
		{
			CameraMatrix::extractFrustumClipPlanes(viewProjMatMainCamera, frustumPlanes[0], frustumPlanes[1],
				frustumPlanes[2], frustumPlanes[3], frustumPlanes[4], frustumPlanes[5], true);
			pBufferFrustumPlanes->UpdateData(frustumPlanes);
		}

		billboardRootConstantBlock.camPos = float4(camPos.getX(), camPos.getY(), camPos.getZ(), 1.f);
		billboardRootConstantBlock.lightPos = float4(lightPos.getX(), lightPos.getY(), lightPos.getZ(), 1.f);
		billboardRootConstantBlock.frustumOn = gUIData.mGeneralSettings.mFrustumOn ? 1 : 0;
		billboardRootConstantBlock.imposter360 = gUIData.mGeneralSettings.mUsing360Imposter ? 1 : 0;
		billboardRootConstantBlock.imposterCount = imposterCount;

		cmdBindPushConstants(cmd, pRootSigCompAngleCompute, billboardConstantIndex, &billboardRootConstantBlock);
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Angle Computation");
		cmdBindPipeline(cmd, pPipelineCompAngleCompute);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetCompAngleCompute);
		cmdDispatch(cmd, imposterCount / 32 + 1, 1, 1);
		cmdEndDebugMarker(cmd);
		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	void DispatchAnimAccelCompute(Cmd* cmd)
	{
		//Skinning accel dispatch.

		//Before dispatch, update joint modelmats.
		pBufferJointModelMats[gFrameIndex]->UpdateData(gStickFigureAnimObject->mJointModelMats.begin());

		cmdBeginDebugMarker(cmd, 1, 0, 1, "AnimAccel Computation");

		cmdBindPipeline(cmd, pPipelineAnimAccelerator);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSetAnimAccelerator[0]);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetAnimAccelerator[1]);
		cmdDispatch(cmd, 1, 1, 1);

		cmdEndDebugMarker(cmd);

		//Aft dispatch, copy results to stick figure object.
		pBufferJointWorldMats[gFrameIndex]->ReadData(gStickFigureAnimObject->mJointWorldMats.begin());
		pBufferJointScales[gFrameIndex]->ReadData(gStickFigureAnimObject->mJointScales.begin());
		pBufferBoneWorldMats[gFrameIndex]->ReadData(gStickFigureAnimObject->mBoneWorldMats.begin());
	}

	////////////////////////////////////////////////////////////////////////////////////
	//									RenderTargets Funcs							  //
	////////////////////////////////////////////////////////////////////////////////////
	void CaptureToRT(Cmd* cmd_)
	{
		//Capture skinning animation to rts.
		cmdBeginGpuTimestampQuery(cmd_, NULL, "Generate Capture of SkinnedMesh");
		
		const uint32_t transformRootConstantIndex = getDescriptorIndexFromName(pRootSignatureSkinning, "transformRootConstant");

		MatrixBlock mvpMatrixBlock;
		mvpMatrixBlock.mProjMat = imposterProjMatrix;
		mvpMatrixBlock.mViewMat = imposterViewMatrix;

		for (int i = 0; i < TextureCount; ++i)
		{
			RenderTarget* renderTarget = rts[i];

			cmdBindRenderTargets(cmd_, 1, &renderTarget, pDepthBuffer, &clearLoadAction, NULL, NULL, -1, -1);
			cmdSetViewport(cmd_, 0.f, 0.f, (float)renderTarget->mWidth, (float)renderTarget->mHeight, 0.f, 1.f);
			cmdSetScissor(cmd_, 0, 0, renderTarget->mWidth, renderTarget->mHeight);
			cmdBindPipeline(cmd_, pPipelineSkinning);
			cmdBindDescriptorSet(cmd_, 0, pDescriptorSetSkinning[0]);
			cmdBindDescriptorSet(cmd_, gFrameIndex, pDescriptorSetSkinning[1]);
			mvpMatrixBlock.mToWorldMat = imposterRotationMatrices[i];
			cmdBindPushConstants(cmd_, pRootSignatureSkinning, transformRootConstantIndex, &mvpMatrixBlock);
			cmdBindVertexBuffer(cmd_, 1, &pGeom->pVertexBuffers[0], pGeom->mVertexStrides, NULL);
			cmdBindIndexBuffer(cmd_, pGeom->pIndexBuffer, pGeom->mIndexType, NULL);
			cmdDrawIndexed(cmd_, pGeom->mIndexCount, 0, 0);
			cmdBindRenderTargets(cmd_, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		}

		cmdEndGpuTimestampQuery(cmd_, NULL);
	}

	void BindDefaultRT(Cmd* cmd_, uint32_t swapChainIndex)
	{
		//Back to default swapchain rtv.
		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapChainIndex];

		RenderTargetBarrier barrier = {pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET};

		cmdResourceBarrier(cmd_, 0, NULL, 0, NULL, 1, &barrier);
		cmdBindRenderTargets(cmd_, 1, &pRenderTarget, pDepthBuffer, &clearLoadAction, NULL, NULL, -1, -1);
		cmdSetViewport(cmd_, 0.f, 0.f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.f, 1.f);
		cmdSetScissor(cmd_, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
	}

	
	////////////////////////////////////////////////////////////////////////////////////
	//									Render Funcs								  //
	////////////////////////////////////////////////////////////////////////////////////
	void RenderQuads(Cmd* cmd)
	{
		//Rendering Quads with capturing.
		constexpr uint32_t stride = sizeof(float) * 6;
		const uint32_t billboardRootConstantIndex = getDescriptorIndexFromName(pRootSignatureQuad, "billboardsRootConstant");

		billboardRootConstantBlock.showQuads = gUIData.mGeneralSettings.mShowQuads ? 1 : 0;
		billboardRootConstantBlock.genShadow = 0;

		cmdBeginGpuTimestampQuery(cmd, NULL, "Render Quads");
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Quad");
		cmdBindPushConstants(cmd, pRootSignatureQuad, billboardRootConstantIndex, &billboardRootConstantBlock);
		cmdBindPipeline(cmd, pPipelineQuad);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorQuad);
		cmdBindVertexBuffer(cmd, 1, &pBufferQuadVertex->buffer, &stride, NULL);
		cmdDrawInstanced(cmd, 6, 0, imposterCount, 0);
		cmdEndDebugMarker(cmd);
		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	void RenderPlane(Cmd* cmd)
	{
		//RenderPlane with scene's depth infos.
		const uint32_t stride = sizeof(float) * 6;
		const uint32_t constantIndex = getDescriptorIndexFromName(pRootSignaturePlane, "lightPOVRootConstant");

		struct Data{
			mat4 lightProjMat;
			int drawShadow;
		}data;

		data.lightProjMat = lightProjMat;
		data.drawShadow = gUIData.mGeneralSettings.mDrawShadows == true ? 1 : 0;
		int drawShadows = gUIData.mGeneralSettings.mDrawShadows ? 1 : 0;

		cmdBeginGpuTimestampQuery(cmd, NULL, "Render Plane");
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Plane");
		cmdBindPipeline(cmd, pPlaneDrawPipeline);
		cmdBindPushConstants(cmd, pRootSignaturePlane, constantIndex, &data);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSet);
		cmdBindVertexBuffer(cmd, 1, &pBufferPlaneVertex->buffer, &stride, NULL);
		cmdDraw(cmd, 6, 0);
		cmdEndDebugMarker(cmd);
		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	void RenderAnimation(Cmd* cmd)
	{
		//Rendering Skinning anims.
		MatrixBlock data;
		data.mProjMat = projViewModelMatrices.mProjMat;
		data.mViewMat = projViewModelMatrices.mViewMat;
		data.mToWorldMat = mat4::identity();

		const uint32_t transformRootConstantIndex = getDescriptorIndexFromName(pRootSignatureSkinning, "transformRootConstant");

		cmdBeginGpuTimestampQuery(cmd, NULL, "Render Skinning Anim");
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Skinned Mesh");
		cmdBindPipeline(cmd, pPipelineSkinning);
		cmdBindPushConstants(cmd, pRootSignatureSkinning, transformRootConstantIndex, &data);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSetSkinning[0]);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetSkinning[1]);
		cmdBindVertexBuffer(cmd, 1, &pGeom->pVertexBuffers[0], pGeom->mVertexStrides, (uint64_t*)NULL);
		cmdBindIndexBuffer(cmd, pGeom->pIndexBuffer, pGeom->mIndexType, (uint64_t)NULL);
		cmdDrawIndexed(cmd, pGeom->mIndexCount, 0, 0);
		cmdEndDebugMarker(cmd);
		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	////////////////////////////////////////////////////////////////////////////////////
	//									Shadow Funcs								  //
	////////////////////////////////////////////////////////////////////////////////////
	void FillShadowDepthRT(Cmd* cmd)
	{
		constexpr uint32_t stride = sizeof(float) * 6;

		const uint32_t billboardRootConstantIndex = getDescriptorIndexFromName(pRootSignatureQuad, "billboardsRootConstant");

		billboardRootConstantBlock.genShadow = 1;

		cmdBeginGpuTimestampQuery(cmd, NULL, "Fill Shadow Depth RT");
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Fill Depth Buffer");
		cmdBindPipeline(cmd, pPipelineQuad);
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorQuad);
		cmdBindRenderTargets(cmd, 1, &shadowRT, shadowDepthRT, &clearLoadAction, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.f, 0.f, (float)mSettings.mWidth, (float)mSettings.mHeight, 0.f, 1.f);
		cmdSetScissor(cmd, 0, 0, mSettings.mWidth, mSettings.mHeight);
		cmdBindPushConstants(cmd, pRootSignatureQuad, billboardRootConstantIndex, &billboardRootConstantBlock);
		cmdBindVertexBuffer(cmd, 1, &pBufferQuadVertex->buffer, &stride, NULL);
		cmdDrawInstanced(cmd, 6, 0, imposterCount, 0);
		cmdEndDebugMarker(cmd);
		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	////////////////////////////////////////////////////////////////////////////////////
	//									Other Funcs 								  //
	////////////////////////////////////////////////////////////////////////////////////
	int GetXZAngle(bool& left)
	{
		//XZ angle computing btw center animating object & camera for debugging purpose.
		vec3 boPosition = vec3(0.f, 0.f, 0.f);
		vec3 boDirection = vec3(0.f, 0.f, 1.f);
		vec3 eyePos = mainCamera->getViewPosition();
		vec3 boToCam = eyePos - boPosition;

		boDirection.setY(0.f);
		boToCam.setY(0.f);

		boToCam = normalize(boToCam);
		boDirection = normalize(boDirection);

		float cosTheta = dot(boToCam, boDirection);
		float theta = acos(cosTheta);
		float angleInDegree = radToDeg(theta);

		vec3 camDir = -boDirection;
		vec3 targetDir = boPosition - eyePos;
		vec3 crossResult = cross(camDir, targetDir);

		left = crossResult.getY() > 0.f ? true : false;

		if (left)
			angleInDegree = 180.f + (180.f - angleInDegree);

		return static_cast<int>(angleInDegree);
	}

	float GetYAngle()
	{
		//Y angle computing btw center animating object & camera for debugging purpose.
		vec3 boPosition = vec3(0.f, 0.f, 0.f);
		vec3 boDirection = vec3(0.f, 1.f, 0.f);
		vec3 eyePos = mainCamera->getViewPosition();
		vec3 boToCam = eyePos - boPosition;

		float boToCamLen = length(boToCam);
		float boDirectionLen = length(boDirection);

		float cosTheta = dot(boToCam, boDirection) / (boToCamLen * boDirectionLen);
		float theta = acos(cosTheta);
		float angleInDegree = radToDeg(theta);

		return angleInDegree;
	}

	//Tried Optimize, put ComputePose Func to the compute shader.
	void UpdateAnims(Cmd* cmd, ProfileToken* pToken)
	{
		cmdBeginGpuTimestampQuery(cmd, NULL, "Skinning calc time");

		//Update the animated object for this frame.
		if (!gStickFigureAnimObject->Update(dtSave))
			LOGF(eINFO, "Animation Not Updating");

		//Pose the rig based on the animated object's updated values.
		if (!gUIData.mGeneralSettings.mShowBindPose && gUIData.mGeneralSettings.mOptimizeAnimSim)
			DispatchAnimAccelCompute(cmd);
		else if(!gUIData.mGeneralSettings.mShowBindPose && !gUIData.mGeneralSettings.mOptimizeAnimSim)
			gStickFigureAnimObject->ComputePose(gStickFigureAnimObject->mRootTransform);
		//Ignore the updated values and pose in bind
		else
			gStickFigureAnimObject->ComputeBindPose(gStickFigureAnimObject->mRootTransform);

		for (unsigned i = 0; i < pGeomData->mJointCount; ++i)
		{
			gUniformDataBones.mBoneMatrix[i] = gStickFigureAnimObject->mJointWorldMats[pGeomData->pJointRemaps[i]] * pGeomData->pInverseBindPoses[i];
		}

		cmdEndGpuTimestampQuery(cmd, NULL);
	}

	const char* GetName() { return "Imposter Rendering"; }
};

DEFINE_APPLICATION_MAIN(ImposterRendering)
