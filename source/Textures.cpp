#include "Textures.h"
#include "VulkanBuffers.h"

#include <wincodec.h>

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
// Releases its interface when it goes out of scope, so the decoding below can return early without leaking. Each one
// is filled once, by the call that takes its address.
template<typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { if (m_Pointer != nullptr) m_Pointer->Release(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** operator&() { return &m_Pointer; }
    T*  operator->() const { return m_Pointer; }
    T*  Get() const { return m_Pointer; }

private:
    T* m_Pointer = nullptr;
};

void ReportError(const char* name, const char* what)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE, "%s: %s\n", name, what);
    OutputDebugString(message);
}

bool InitializeCom()
{
    static const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
}

std::wstring TexturePath(const char* name)
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    const std::string directory = exePath;
    const std::string path      = directory.substr(0, directory.find_last_of('\\') + 1) + "textures\\" + name;
    const int         length    = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);

    if (length <= 0)
        return {};

    std::wstring wide((size_t)length - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);

    return wide;
}

bool DecodeImageFile(const char* name, std::vector<uint8_t>& rgba, VkExtent2D& extent)
{
    if (!InitializeCom())
    {
        ReportError(name, "couldn't initialize COM, which the image decoding needs.");
        return false;
    }

    ComPtr<IWICImagingFactory> factory;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory)))
    {
        ReportError(name, "couldn't create the Windows Imaging Component factory.");
        return false;
    }

    const std::wstring path = TexturePath(name);

    ComPtr<IWICBitmapDecoder> decoder;

    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
    {
        ReportError(name, "couldn't open the file in the textures folder, or no codec reads it.");
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;

    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        ReportError(name, "the file has no first frame to read.");
        return false;
    }

    ComPtr<IWICBitmapSource> converted;

    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.Get(), &converted)))
    {
        ReportError(name, "the pixels couldn't be converted to 8 bit RGBA.");
        return false;
    }

    UINT width = 0;
    UINT height = 0;

    if (FAILED(converted->GetSize(&width, &height)) || width == 0 || height == 0)
    {
        ReportError(name, "the image has no size.");
        return false;
    }

    const UINT stride = width * 4;

    rgba.resize((size_t)stride * height);

    if (FAILED(converted->CopyPixels(nullptr, stride, (UINT)rgba.size(), rgba.data())))
    {
        ReportError(name, "the pixels couldn't be read.");
        return false;
    }

    extent = { width, height };

    return true;
}
}

Image CreateTexture(const void* rgba, VkExtent2D extent, const char* name)
{
    const VkDeviceSize size = (VkDeviceSize)extent.width * extent.height * 4;

    Image image = CreateImage(extent, VK_FORMAT_R8G8B8A8_SRGB, 1, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, name);

    if (image.image == VK_NULL_HANDLE)
        return {};

    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Texture staging");

    if (staging.mapped == nullptr)
    {
        DestroyBuffer(staging);
        DestroyImage(image);
        return {};
    }

    memcpy(staging.mapped, rgba, (size_t)size);

    VkCommandBuffer commandBuffer = BeginOneTimeCommands();

    TransitionImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1);

    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { extent.width, extent.height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // From here the hit shaders only sample it, and nothing writes it again.
    TransitionImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 1);

    EndOneTimeCommands(commandBuffer);

    DestroyBuffer(staging);

    return image;
}

Image LoadTexture(const char* name)
{
    std::vector<uint8_t> rgba;
    VkExtent2D           extent = {};

    if (!DecodeImageFile(name, rgba, extent))
        return {};

    return CreateTexture(rgba.data(), extent, name);
}

Image CreateWhiteTexture()
{
    const uint8_t white[4] = { 255, 255, 255, 255 };

    return CreateTexture(white, { 1, 1 }, "White");
}
