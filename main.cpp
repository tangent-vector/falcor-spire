// main.cpp

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif


#include "Falcor.h"
#include "Graphics\Scene\SceneImporter.h"

#include "Externals/dear_imgui/imgui.h"

using namespace Falcor;

namespace Falcor
{
    extern uint32_t gRootSignatureSets;
    extern uint32_t gRootSignatureSwitches;
    extern void* gLastRootSignature;
}

class ModelViewer : public Sample //, public SampleTest
{
public:
    void onLoad() override;
    void onFrameRender() override;
    void onShutdown() override;
    void onResizeSwapChain() override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    void onGuiRender() override;



//

    // The currently loaded scene
    Scene::SharedPtr mpScene;
    SceneRenderer::UniquePtr mpSceneRenderer;

    void loadScene()
    {
        std::string path;
        if(openFileDialog(Scene::kFileFormatString, path))
        {
            loadSceneFromFile(path);
        }
    }

    void loadSceneFromFile(std::string const& path)
    {
        mpScene = SceneImporter::loadScene(path, Model::GenerateTangentSpace, Scene::LoadMaterialHistory);

        mpSceneRenderer = SceneRenderer::create(mpScene);
//        mpSceneRenderer->setCameraControllerType(SceneRenderer::CameraControllerType::FirstPerson);

        resetCamera();

        // TIM: try to enforce consistency of samplers!
        uint32_t modelCount = mpScene->getModelCount();
        for(uint32_t mm = 0; mm < modelCount; ++mm)
        {
            mpScene->getModel(mm)->bindSamplerToMaterials(mpLinearSampler);
        }
    }

    void resetCamera();

    bool mUseTriLinearFiltering = true;
    Sampler::SharedPtr mpPointSampler = nullptr;
    Sampler::SharedPtr mpLinearSampler = nullptr;

    GraphicsProgram::SharedPtr mpProgram = nullptr;
    GraphicsVars::SharedPtr mpProgramVars = nullptr;
    GraphicsState::SharedPtr mpGraphicsState = nullptr;

    Camera::SharedPtr mpCamera;
    FirstPersonCameraController mFirstPersonCameraController;
    CameraController& getActiveCameraController()
    {
        return mFirstPersonCameraController;
    }


    bool mDrawWireframe = false;
    bool mAnimate = false;
    bool mGenerateTangentSpace = true;
    glm::vec3 mAmbientIntensity = glm::vec3(0.1f, 0.1f, 0.1f);

    uint32_t mActiveAnimationID = kBindPoseAnimationID;
    static const uint32_t kBindPoseAnimationID = AnimationController::kBindPoseAnimationId;

    RasterizerState::SharedPtr mpWireframeRS = nullptr;
    RasterizerState::SharedPtr mpCullRastState[3]; // 0 = no culling, 1 = backface culling, 2 = frontface culling
    uint32_t mCullMode = 1;

    DepthStencilState::SharedPtr mpNoDepthDS = nullptr;
    DepthStencilState::SharedPtr mpDepthTestDS = nullptr;


    // Note: stuff copied from Falcor "feature demo"
    ToneMapping::UniquePtr mpToneMapper;

    Fbo::SharedPtr mpMainFbo;
    Fbo::SharedPtr mpResolveFbo;
    Fbo::SharedPtr mpPostProcessFbo;

    uint32_t mSampleCount = 4;
};

static bool gIsProfiling;
static int32_t gProfileFrameCount;
static int32_t gProfileStopFrameCount = 100;
static Falcor::CpuTimer::TimePoint gProfileStartTime;
static Falcor::CpuTimer::TimePoint gProfileStopTime;


void ModelViewer::onGuiRender()
{
    // Stats
    ImGui::Text("Root Signature Sets: %d", gRootSignatureSets);
    ImGui::Text("Root Signature Switches: %d", gRootSignatureSwitches);

    // Load a scene
    if( mpGui->addButton("Load Scene") )
    {
        loadScene();
    }

    if( mpGui->addButton("Generate Benchmark Data") )
    {
        gIsProfiling = true;
        gProfileFrameCount = 0;
    }
    mpGui->addIntVar("Frames to Benchmark", gProfileStopFrameCount, 1, 100000);
    mpGui->addSeparator();

    mpGui->addCheckBox("Enable Falcor Profiler", gProfileEnabled);
    mpGui->addSeparator();

    mpGui->addCheckBox("Wireframe", mDrawWireframe);
    mpGui->addCheckBox("TriLinear Filtering", mUseTriLinearFiltering);

    Gui::DropdownList cullList;
    cullList.push_back({ 0, "No Culling" });
    cullList.push_back({ 1, "Backface Culling" });
    cullList.push_back({ 2, "Frontface Culling" });
    mpGui->addDropdown("Cull Mode", cullList, mCullMode);


    Gui::DropdownList kSampleCountList = {
        { 1, "1"},
        { 2, "2" },
        { 4, "4" },
        { 8, "8" },
    };

    // Stuff from Falcor "feature demo"
    if (mpGui->addDropdown("Sample Count", kSampleCountList, mSampleCount))
    {
        onResizeSwapChain();
    }


    mpToneMapper->renderUI(mpGui.get(), "Tone Mapping");
}

void ModelViewer::onLoad()
{
    mpCamera = Camera::create();
#ifdef FALCOR_SPIRE_SUPPORTED
    mpProgram = GraphicsProgram::createFromSpireFile("shaders.spire");
#else
    mpProgram = GraphicsProgram::createFromFile("", "shaders.hlsl");
#endif

    // create rasterizer state
    RasterizerState::Desc wireframeDesc;
    wireframeDesc.setFillMode(RasterizerState::FillMode::Wireframe);
    wireframeDesc.setCullMode(RasterizerState::CullMode::None);
    mpWireframeRS = RasterizerState::create(wireframeDesc);

    RasterizerState::Desc solidDesc;
    solidDesc.setCullMode(RasterizerState::CullMode::None);
    mpCullRastState[0] = RasterizerState::create(solidDesc);
    solidDesc.setCullMode(RasterizerState::CullMode::Back);
    mpCullRastState[1] = RasterizerState::create(solidDesc);
    solidDesc.setCullMode(RasterizerState::CullMode::Front);
    mpCullRastState[2] = RasterizerState::create(solidDesc);

    // Depth test
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthTest(false);
    mpNoDepthDS = DepthStencilState::create(dsDesc);
    dsDesc.setDepthTest(true);
    mpDepthTestDS = DepthStencilState::create(dsDesc);

    mFirstPersonCameraController.attachCamera(mpCamera);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    mpPointSampler = Sampler::create(samplerDesc);
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearSampler = Sampler::create(samplerDesc);


#ifdef FALCOR_SPIRE_SUPPORTED
    mpProgramVars = GraphicsVars::create(mpProgram);
#else
    mpProgramVars = GraphicsVars::create(mpProgram->getActiveVersion()->getReflector());
#endif
    mpGraphicsState = GraphicsState::create();
    mpGraphicsState->setProgram(mpProgram);

    std::vector<ArgList::Arg> scenePaths = mArgList.getValues("scene");
    if (!scenePaths.empty())
    {
        loadSceneFromFile(scenePaths[0].asString());
    }


    // Stuff from Falcor "feature demo"
    mpToneMapper = ToneMapping::create(ToneMapping::Operator::HableUc2);
}

void ModelViewer::onFrameRender()
{
    gRootSignatureSets = 0;
    gRootSignatureSwitches = 0;
    gLastRootSignature = 0;

    if(gIsProfiling && gProfileFrameCount == 0)
    {
        gProfileStartTime = Falcor::CpuTimer::getCurrentTimePoint();
    }

#if 0
    const glm::vec4 clearColor(0.38f, 0.52f, 0.10f, 1);
    mpRenderContext->clearFbo(mpDefaultFBO.get(), clearColor, 1.0f, 0, FboAttachmentType::All);
    mpGraphicsState->setFbo(mpDefaultFBO);
#else
    mpRenderContext->pushGraphicsState(mpGraphicsState);
    mpGraphicsState->setFbo(mpMainFbo);
    mpRenderContext->clearFbo(mpMainFbo.get(), glm::vec4(), 1, 0, FboAttachmentType::All);
    mpRenderContext->clearFbo(mpPostProcessFbo.get(), glm::vec4(), 1, 0, FboAttachmentType::Color);
#endif

    if( mpScene )
    {
        getActiveCameraController().update();


        //




        //



//        mpGraphicsState->setRasterizerState(mpCullRastState[mCullMode]);
//        mpGraphicsState->setDepthStencilState(mpDepthTestDS);

#ifdef FALCOR_SPIRE_SUPPORTED
#else
        setSceneLightsIntoConstantBuffer(mpScene.get(), mpProgramVars["PerFrameCB"].get());
#endif

        mpGraphicsState->setProgram(mpProgram);
//        mpRenderContext->setGraphicsState(mpGraphicsState);
        mpRenderContext->setGraphicsVars(mpProgramVars);

        mpSceneRenderer->renderScene(mpRenderContext.get(), mpCamera.get());


        // Resolve MSAA surface
        mpRenderContext->blit(mpMainFbo->getColorTexture(0)->getSRV(), mpResolveFbo->getRenderTargetView(0));
        mpRenderContext->blit(mpMainFbo->getColorTexture(1)->getSRV(), mpResolveFbo->getRenderTargetView(1));
        mpRenderContext->blit(mpMainFbo->getDepthStencilTexture()->getSRV(), mpResolveFbo->getRenderTargetView(2));

        // Apply tone mapping
        mpToneMapper->execute(mpRenderContext.get(), mpResolveFbo, mpDefaultFBO);

    }

    mpRenderContext->popGraphicsState();


    renderText("HELLO WORLD", glm::vec2(10, 30));

    if(gIsProfiling)
    {
        gProfileFrameCount++;

        if( gProfileFrameCount == gProfileStopFrameCount)
        {
            gIsProfiling = false;
            gProfileStopTime = Falcor::CpuTimer::getCurrentTimePoint();

            float duration = Falcor::CpuTimer::calcDuration(gProfileStartTime, gProfileStopTime);

            FILE* file = fopen(
                "stats-"
#ifdef FALCOR_SPIRE_SUPPORTED
                "spire"
#else
                "original"
#endif
                ".txt", "w");
            fprintf(file, "%d %f %f\n", gProfileFrameCount, duration, duration / gProfileFrameCount);
            fclose(file);
        }


    }
}

void ModelViewer::onShutdown()
{

}

bool ModelViewer::onKeyEvent(const KeyboardEvent& keyEvent)
{
    bool bHandled = getActiveCameraController().onKeyEvent(keyEvent);
    if(bHandled == false)
    {
        if(keyEvent.type == KeyboardEvent::Type::KeyPressed)
        {
            switch(keyEvent.key)
            {
            case KeyboardEvent::Key::R:
                resetCamera();
                bHandled = true;
                break;
            }
        }
    }
    return bHandled;
}

bool ModelViewer::onMouseEvent(const MouseEvent& mouseEvent)
{
    return getActiveCameraController().onMouseEvent(mouseEvent);
}

void ModelViewer::onResizeSwapChain()
{

#if 0

    float height = (float)mpDefaultFBO->getHeight();
    float width = (float)mpDefaultFBO->getWidth();

    mpCamera->setFovY(float(M_PI / 3));
    float aspectRatio = (width / height);
    mpCamera->setAspectRatio(aspectRatio);
#endif


    // Create the main FBO
    uint32_t w = mpDefaultFBO->getWidth();
    uint32_t h = mpDefaultFBO->getHeight();

    Fbo::Desc fboDesc;
    fboDesc.setColorTarget(0, ResourceFormat::RGBA8UnormSrgb);
    mpPostProcessFbo = FboHelper::create2D(w, h, fboDesc);

    fboDesc.setColorTarget(0, ResourceFormat::RGBA32Float).setColorTarget(1, ResourceFormat::RGBA8Unorm).setColorTarget(2, ResourceFormat::R32Float);
    mpResolveFbo = FboHelper::create2D(w, h, fboDesc);

    fboDesc.setSampleCount(mSampleCount).setColorTarget(2, ResourceFormat::Unknown).setDepthStencilTarget(ResourceFormat::D32Float);
    mpMainFbo = FboHelper::create2D(w, h, fboDesc);

    if( mpSceneRenderer )
    {
        mpSceneRenderer->getScene()->getActiveCamera()->setAspectRatio((float)w / (float)h);
        mpCamera->setAspectRatio((float)w / (float)h);
    }
}

void ModelViewer::resetCamera()
{
    if( mpScene )
    {
        auto pSceneCamera = mpScene->getActiveCamera();
        if( !pSceneCamera )
        {
            if( mpScene->getCameraCount() )
            {
                pSceneCamera = mpScene->getCamera(0);
            }
        }

        if( pSceneCamera )
        {
            mpCamera->setPosition(pSceneCamera->getPosition());
            mpCamera->setTarget(pSceneCamera->getTarget());
            mpCamera->setUpVector(pSceneCamera->getUpVector());

            mpCamera->setDepthRange(
                pSceneCamera->getNearPlane(),
                pSceneCamera->getFarPlane());
        }
        else
        {
            mpCamera->setPosition(glm::vec3(0, 0, -10));
            mpCamera->setTarget(glm::vec3(0,0,0));
            mpCamera->setUpVector(glm::vec3(0,1,0));

            mpCamera->setDepthRange(0.1f, 1000.0f);
        }

        mFirstPersonCameraController.setCameraSpeed(1.0f);
    }
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    ModelViewer modelViewer;
    SampleConfig config;
    config.windowDesc.title = "Falcor"
#ifdef FALCOR_SPIRE_SUPPORTED
    "+Spire"
#endif
    " Scene Viewer";
    config.windowDesc.resizableWindow = true;
    modelViewer.run(config);
}
