#include "DesktopCapture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopedValueRollback>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstring>

namespace {

using Microsoft::WRL::ComPtr;

QString failure(const char *operation,HRESULT result)
{
    return QStringLiteral("desktop_capture: %1 failed (0x%2)")
        .arg(QString::fromLatin1(operation))
        .arg(qulonglong(quint32(result)),8,16,QLatin1Char('0'));
}

struct AcquiredFrame {
    IDXGIOutputDuplication *duplication=nullptr;
    ~AcquiredFrame(){if(duplication)duplication->ReleaseFrame();}
};

struct MappedTexture {
    ID3D11DeviceContext *context=nullptr;
    ID3D11Texture2D *texture=nullptr;
    ~MappedTexture(){if(context&&texture)context->Unmap(texture,0);}
};

} // namespace
#endif

QImage captureDesktopRegion(const QRect &physicalBounds,QString &error)
{
    error.clear();
#ifdef Q_OS_WIN
    static thread_local bool capturing=false;
    if(capturing){
        error=QStringLiteral("desktop_capture: recursive capture is not supported");
        return {};
    }
    QScopedValueRollback<bool> captureGuard(capturing,true);
    if(physicalBounds.isEmpty()){
        error=QStringLiteral("desktop_capture: empty physical capture bounds");
        return {};
    }
    const qint64 left=physicalBounds.x(),top=physicalBounds.y();
    const qint64 right=left+physicalBounds.width(),bottom=top+physicalBounds.height();

    ComPtr<IDXGIFactory1> factory;
    HRESULT result=CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if(FAILED(result)){error=failure("CreateDXGIFactory1",result);return {};}

    ComPtr<IDXGIAdapter1> selectedAdapter;
    ComPtr<IDXGIOutput1> selectedOutput;
    DXGI_OUTPUT_DESC outputDescription{};
    for(UINT adapterIndex=0;!selectedOutput;++adapterIndex){
        ComPtr<IDXGIAdapter1> adapter;
        result=factory->EnumAdapters1(adapterIndex,adapter.GetAddressOf());
        if(result==DXGI_ERROR_NOT_FOUND)break;
        if(FAILED(result)){error=failure("EnumAdapters1",result);return {};}
        for(UINT outputIndex=0;;++outputIndex){
            ComPtr<IDXGIOutput> output;
            result=adapter->EnumOutputs(outputIndex,output.GetAddressOf());
            if(result==DXGI_ERROR_NOT_FOUND)break;
            if(FAILED(result)){error=failure("EnumOutputs",result);return {};}
            DXGI_OUTPUT_DESC description{};
            result=output->GetDesc(&description);
            if(FAILED(result)){error=failure("IDXGIOutput::GetDesc",result);return {};}
            const auto &monitor=description.DesktopCoordinates;
            if(!description.AttachedToDesktop||left<monitor.left||top<monitor.top||
               right>monitor.right||bottom>monitor.bottom)continue;
            if(description.Rotation!=DXGI_MODE_ROTATION_IDENTITY){
                error=QStringLiteral("desktop_capture: rotated monitors are not supported by this self-test capture");
                return {};
            }
            result=output.As(&selectedOutput);
            if(FAILED(result)){error=failure("QueryInterface IDXGIOutput1",result);return {};}
            selectedAdapter=adapter;
            outputDescription=description;
            break;
        }
    }
    if(!selectedOutput){
        error=QStringLiteral("desktop_capture: physical capture bounds are outside one attached monitor");
        return {};
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    result=D3D11CreateDevice(selectedAdapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
        device.GetAddressOf(),nullptr,context.GetAddressOf());
    if(FAILED(result)){error=failure("D3D11CreateDevice on monitor adapter",result);return {};}

    ComPtr<IDXGIOutputDuplication> duplication;
    result=selectedOutput->DuplicateOutput(device.Get(),duplication.GetAddressOf());
    if(FAILED(result)){error=failure("DuplicateOutput",result);return {};}
    DXGI_OUTDUPL_DESC duplicationDescription{};
    duplication->GetDesc(&duplicationDescription);
    if(duplicationDescription.Rotation!=DXGI_MODE_ROTATION_IDENTITY){
        error=QStringLiteral("desktop_capture: duplication surface rotation changed");
        return {};
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    // Duplication supplies desktop updates. A single long wait on the UI thread
    // would also prevent this test window's timers and native renderer from
    // producing those updates. Bound the retry budget while allowing drawing;
    // user input and socket notifier events stay queued until capture finishes.
    QElapsedTimer deadline;
    deadline.start();
    do {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents|QEventLoop::ExcludeSocketNotifiers,5);
        result=duplication->AcquireNextFrame(10,&frameInfo,desktopResource.GetAddressOf());
    } while(result==DXGI_ERROR_WAIT_TIMEOUT&&deadline.elapsed()<500);
    if(FAILED(result)){error=failure("AcquireNextFrame (500 ms retry budget)",result);return {};}
    AcquiredFrame frame{duplication.Get()};

    ComPtr<ID3D11Texture2D> desktopTexture;
    result=desktopResource.As(&desktopTexture);
    if(FAILED(result)){error=failure("QueryInterface desktop texture",result);return {};}
    D3D11_TEXTURE2D_DESC sourceDescription{};
    desktopTexture->GetDesc(&sourceDescription);
    if(sourceDescription.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||sourceDescription.SampleDesc.Count!=1){
        error=QStringLiteral("desktop_capture: unsupported desktop surface format or sample count");
        return {};
    }
    const qint64 cropLeft=left-outputDescription.DesktopCoordinates.left;
    const qint64 cropTop=top-outputDescription.DesktopCoordinates.top;
    const qint64 cropRight=cropLeft+physicalBounds.width();
    const qint64 cropBottom=cropTop+physicalBounds.height();
    if(cropLeft<0||cropTop<0||cropRight>sourceDescription.Width||cropBottom>sourceDescription.Height){
        error=QStringLiteral("desktop_capture: monitor dimensions changed before GPU crop");
        return {};
    }

    // Never map the full desktop resource. Crop the caller-validated owned region
    // on the GPU first, into a staging allocation of precisely the requested size.
    D3D11_TEXTURE2D_DESC stagingDescription{};
    stagingDescription.Width=UINT(physicalBounds.width());
    stagingDescription.Height=UINT(physicalBounds.height());
    stagingDescription.MipLevels=1;
    stagingDescription.ArraySize=1;
    stagingDescription.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDescription.SampleDesc.Count=1;
    stagingDescription.Usage=D3D11_USAGE_STAGING;
    stagingDescription.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> croppedTexture;
    result=device->CreateTexture2D(&stagingDescription,nullptr,croppedTexture.GetAddressOf());
    if(FAILED(result)){error=failure("CreateTexture2D cropped staging",result);return {};}
    const D3D11_BOX crop{UINT(cropLeft),UINT(cropTop),0,UINT(cropRight),UINT(cropBottom),1};
    context->CopySubresourceRegion(croppedTexture.Get(),0,0,0,0,desktopTexture.Get(),0,&crop);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result=context->Map(croppedTexture.Get(),0,D3D11_MAP_READ,0,&mapped);
    if(FAILED(result)){error=failure("Map cropped staging",result);return {};}
    MappedTexture mapping{context.Get(),croppedTexture.Get()};
    const qsizetype rowBytes=qsizetype(physicalBounds.width())*4;
    if(!mapped.pData||quint64(mapped.RowPitch)<quint64(rowBytes)){
        error=QStringLiteral("desktop_capture: invalid cropped staging row pitch");
        return {};
    }
    QImage image(physicalBounds.size(),QImage::Format_ARGB32);
    if(image.isNull()){
        error=QStringLiteral("desktop_capture: cannot allocate cropped image");
        return {};
    }
    for(int y=0;y<image.height();++y){
        auto row=image.scanLine(y);
        const auto source=static_cast<const unsigned char*>(mapped.pData)+qsizetype(y)*mapped.RowPitch;
        std::memcpy(row,source,size_t(rowBytes));
        // Desktop alpha is not meaningful; preserve RGB and make the region opaque.
        auto pixels=reinterpret_cast<QRgb*>(row);
        for(int x=0;x<image.width();++x)pixels[x]|=0xff000000u;
    }
    return image;
#else
    Q_UNUSED(physicalBounds);
    return {};
#endif
}
