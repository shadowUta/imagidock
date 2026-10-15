#include "RenderScene.h"
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/LightManager.h>
#include <filament/IndirectLight.h>
#include <filament/Skybox.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/FilamentInstance.h>
#include <gltfio/Animator.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>
#include <QThread>
#include <cmath>
#include <atomic>
#include <algorithm>
using namespace filament;
using namespace filament::math;
using namespace filament::gltfio;
struct RenderScene::Impl {
    Engine* engine=nullptr;Renderer* renderer=nullptr;SwapChain* chain=nullptr;Scene* scene=nullptr;View* view=nullptr;Camera* camera=nullptr;
    Skybox* sky=nullptr;IndirectLight* indirect=nullptr;utils::Entity cameraEntity;std::vector<utils::Entity> lights;
    MaterialProvider* materials=nullptr;AssetLoader* loader=nullptr;FilamentAsset* asset=nullptr;TextureProvider* stb=nullptr;TextureProvider* ktx=nullptr;
    std::unique_ptr<ResourceLoader> resources;bool loading=false,added=false;QString error;QJsonObject metadata;int width=1,height=1;
    float3 center={0,0,0},offset={0,0,0};float radius=1,distance=3,yaw=.65f,pitch=.35f;
    void updateCamera(){if(!camera)return;float3 target=center+offset;float3 eye=target+float3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)}*distance;camera->lookAt(eye,target);camera->setProjection(45.0,double(width)/std::max(1,height),std::max(.001f,distance/1000),std::max(100.f,distance+radius*20),Camera::Fov::VERTICAL);}
    void unload(){if(resources)resources->asyncCancelLoad();resources.reset();if(asset){scene->removeEntities(asset->getEntities(),asset->getEntityCount());loader->destroyAsset(asset);asset=nullptr;}loading=false;added=false;}
};
RenderScene::RenderScene(void* window,int width,int height):d(std::make_unique<Impl>()){
    d->width=std::max(1,width);d->height=std::max(1,height);
    d->engine=Engine::create(backend::Backend::OPENGL);if(!d->engine){d->error=QStringLiteral("无法创建 OpenGL 3D 渲染器");return;}
    auto* e=d->engine;d->renderer=e->createRenderer();d->chain=window?e->createSwapChain(window):e->createSwapChain(d->width,d->height);
    d->scene=e->createScene();d->view=e->createView();d->view->setScene(d->scene);d->view->setViewport({0,0,uint32_t(d->width),uint32_t(d->height)});
    d->cameraEntity=utils::EntityManager::get().create();d->camera=e->createCamera(d->cameraEntity);d->camera->setExposure(16.f,1.f/125.f,100.f);d->view->setCamera(d->camera);
    d->view->setAmbientOcclusionOptions({.enabled=true});d->view->setAntiAliasing(View::AntiAliasing::FXAA);
    d->renderer->setClearOptions({.clearColor={.055f,.065f,.085f,1.f},.clear=true,.discard=true});
    d->sky=Skybox::Builder().color({.055f,.065f,.085f,1}).build(*e);d->scene->setSkybox(d->sky);
    float3 sh[9]={{.6f,.65f,.75f},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
    d->indirect=IndirectLight::Builder().irradiance(3,sh).intensity(24000).build(*e);d->scene->setIndirectLight(d->indirect);
    auto light=utils::EntityManager::get().create();LightManager::Builder(LightManager::Type::DIRECTIONAL).color({1.f,.94f,.85f}).intensity(85000).direction({-.5f,-.8f,-.6f}).castShadows(true).build(*e,light);d->lights.push_back(light);d->scene->addEntity(light);
    auto fill=utils::EntityManager::get().create();LightManager::Builder(LightManager::Type::DIRECTIONAL).color({.6f,.75f,1.f}).intensity(25000).direction({.8f,-.25f,.5f}).castShadows(false).build(*e,fill);d->lights.push_back(fill);d->scene->addEntity(fill);
    d->materials=createUbershaderProvider(e,UBERARCHIVE_DEFAULT_DATA,UBERARCHIVE_DEFAULT_SIZE);d->loader=AssetLoader::create({e,d->materials});d->stb=createStbProvider(e);d->ktx=createKtx2Provider(e);d->updateCamera();
}
RenderScene::~RenderScene(){if(!d->engine)return;auto* e=d->engine;d->unload();AssetLoader::destroy(&d->loader);delete d->stb;delete d->ktx;d->materials->destroyMaterials();delete d->materials;e->destroy(d->view);e->destroy(d->scene);e->destroy(d->sky);e->destroy(d->indirect);for(auto l:d->lights){e->destroy(l);utils::EntityManager::get().destroy(l);}e->destroyCameraComponent(d->cameraEntity);utils::EntityManager::get().destroy(d->cameraEntity);e->destroy(d->renderer);e->destroy(d->chain);Engine::destroy(&d->engine);}
bool RenderScene::load(const PreparedModel& prepared){
    if(!d->engine)return false;if(!prepared.error.isEmpty()){d->error=prepared.error;return false;}d->unload();d->error.clear();d->metadata=prepared.metadata;
    d->asset=d->loader->createAsset(reinterpret_cast<const uint8_t*>(prepared.bytes.constData()),uint32_t(prepared.bytes.size()));
    if(!d->asset){d->error=QStringLiteral("Filament 无法解析此模型");return false;}
    auto p=prepared.resourcePath.toUtf8();d->resources=std::make_unique<ResourceLoader>(ResourceConfiguration{d->engine,p.constData(),true});
    d->resources->addTextureProvider("image/png",d->stb);d->resources->addTextureProvider("image/jpeg",d->stb);d->resources->addTextureProvider("image/ktx2",d->ktx);
    if(!d->resources->asyncBeginLoad(d->asset)){d->error=QStringLiteral("模型资源加载失败");return false;}d->loading=true;
    auto box=d->asset->getBoundingBox();d->center=box.center();d->radius=std::max(.01f,length(box.extent()));resetCamera();return true;
}
void RenderScene::resize(int width,int height){d->width=std::max(1,width);d->height=std::max(1,height);if(d->view)d->view->setViewport({0,0,uint32_t(d->width),uint32_t(d->height)});d->updateCamera();}
void RenderScene::orbit(float dx,float dy){d->yaw-=dx*.008f;d->pitch=std::clamp(d->pitch+dy*.008f,-1.5f,1.5f);d->updateCamera();}
void RenderScene::pan(float dx,float dy){float scale=d->distance/std::max(1,d->height);d->offset+=float3{-std::cos(d->yaw)*dx,dy,std::sin(d->yaw)*dx}*scale;d->updateCamera();}
void RenderScene::zoom(float delta){d->distance=std::clamp(d->distance*std::exp(-delta*.001f),d->radius*.1f,d->radius*100.f);d->updateCamera();}
void RenderScene::resetCamera(){d->yaw=.65f;d->pitch=.35f;d->offset={0,0,0};d->distance=d->radius*2.8f;d->updateCamera();}
void RenderScene::animate(float time){if(!d->asset||d->loading)return;auto* a=d->asset->getInstance()->getAnimator();if(a->getAnimationCount()){float duration=a->getAnimationDuration(0);a->applyAnimation(0,duration>0?std::fmod(time,duration):0);a->updateBoneMatrices();}}
void RenderScene::render(){
    if(!d->engine)return;
    if(d->loading){d->resources->asyncUpdateLoad();if(d->resources->asyncGetLoadProgress()>=1.f){d->loading=false;d->scene->addEntities(d->asset->getEntities(),d->asset->getEntityCount());d->added=true;}}
    if(d->renderer->beginFrame(d->chain)){d->renderer->render(d->view);d->renderer->endFrame();}d->engine->pumpMessageQueues();
}
QImage RenderScene::snapshot(){
    if(!d->engine)return {};
    for(int i=0;d->loading&&i<3000;++i){render();QThread::msleep(2);}if(d->loading){d->error="Model load timeout";return {};}
    for(int i=0;i<8;++i){render();d->engine->flushAndWait();}
    struct Readback { QImage image;std::atomic<bool> done=false; };
    auto readback=std::make_shared<Readback>();readback->image=QImage(d->width,d->height,QImage::Format_RGBA8888);readback->image.fill(Qt::transparent);
    auto* callbackState=new std::shared_ptr<Readback>(readback);
    backend::PixelBufferDescriptor pixels(readback->image.bits(),readback->image.sizeInBytes(),backend::PixelDataFormat::RGBA,backend::PixelDataType::UBYTE,[](void*,size_t,void* user){auto* state=static_cast<std::shared_ptr<Readback>*>(user);(*state)->done.store(true);delete state;},callbackState);
    if(!d->renderer->beginFrame(d->chain)){d->engine->flushAndWait();if(!d->renderer->beginFrame(d->chain))return {};}
    d->renderer->render(d->view);d->renderer->readPixels(0,0,d->width,d->height,std::move(pixels));d->renderer->endFrame();d->engine->flushAndWait();
    for(int i=0;!readback->done&&i<1000;++i){d->engine->pumpMessageQueues();QThread::msleep(1);}
    // Callback owns the destination even on timeout; no driver write can outlive its memory.
    if(!readback->done){d->error="Snapshot callback timeout";return {};}
    // Filament normalizes readPixels to top-first client rows, including OpenGL.
    // An additional OpenGL-style Y flip would invert every saved thumbnail.
    return readback->image;
}
QString RenderScene::error()const{return d->error;}
QJsonObject RenderScene::metadata()const{return d->metadata;}
