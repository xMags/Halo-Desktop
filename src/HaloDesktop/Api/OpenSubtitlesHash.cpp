#include "pch.h"
#include "Api/OpenSubtitlesHash.h"
#include "Api/HttpExecutor.h"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    constexpr std::uint64_t Chunk=65536;
    concurrency::task<winrt::Windows::Web::Http::HttpResponseMessage> Send(std::shared_ptr<HaloDesktop::Api::HttpExecutor>const&executor,winrt::hstring const&url,winrt::Windows::Web::Http::HttpMethod const&method,std::optional<winrt::hstring>range={}){winrt::Windows::Web::Http::HttpRequestMessage request{method,winrt::Windows::Foundation::Uri{url}};if(range)request.Headers().TryAppendWithoutValidation(L"Range",*range);co_return co_await executor->SendForStreamAsync(request);}
    concurrency::task<std::uint64_t> Size(winrt::hstring const&url,std::shared_ptr<HaloDesktop::Api::HttpExecutor>const&executor){auto head=co_await Send(executor,url,winrt::Windows::Web::Http::HttpMethod::Head());auto length=head.Content().Headers().ContentLength();if(head.IsSuccessStatusCode()&&length&&length.Value()>0)co_return length.Value();auto probe=co_await Send(executor,url,winrt::Windows::Web::Http::HttpMethod::Get(),L"bytes=0-0");if(static_cast<std::uint16_t>(probe.StatusCode())!=206)throw std::runtime_error("The source does not support range hashing.");auto range=probe.Content().Headers().ContentRange();auto total=range?range.Length():nullptr;if(!total||total.Value()==0)throw std::runtime_error("The source did not report its size.");co_return total.Value();}
    concurrency::task<std::vector<std::uint8_t>> Range(winrt::hstring const&url,std::shared_ptr<HaloDesktop::Api::HttpExecutor>const&executor,std::uint64_t start,std::uint64_t end){auto response=co_await Send(executor,url,winrt::Windows::Web::Http::HttpMethod::Get(),L"bytes="+winrt::to_hstring(start)+L"-"+winrt::to_hstring(end));if(static_cast<std::uint16_t>(response.StatusCode())!=206)throw std::runtime_error("The source ignored a range request.");auto buffer=co_await response.Content().ReadAsBufferAsync();if(buffer.Length()>Chunk)throw std::runtime_error("The source returned an oversized hash range.");auto reader=winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);std::vector<std::uint8_t>bytes(buffer.Length());reader.ReadBytes(bytes);co_return bytes;}
    std::uint64_t Sum(std::vector<std::uint8_t>const&bytes)noexcept{std::uint64_t sum{};for(std::size_t offset=0;offset<bytes.size();offset+=8){std::uint64_t word{};auto const count=(std::min)(std::size_t{8},bytes.size()-offset);for(std::size_t i=0;i<count;++i)word|=static_cast<std::uint64_t>(bytes[offset+i])<<(i*8);sum+=word;}return sum;}
}
namespace HaloDesktop::Api
{
    concurrency::task<VideoHashResult> ComputeRemoteVideoHashAsync(winrt::hstring url,std::shared_ptr<HttpExecutor>executor){auto const size=co_await Size(url,executor);if(size<Chunk*2)throw std::runtime_error("The source is too small for moviehash.");auto head=co_await Range(url,executor,0,Chunk-1);auto tail=co_await Range(url,executor,size-Chunk,size-1);auto const hash=size+Sum(head)+Sum(tail);std::wostringstream text;text<<std::hex<<std::nouppercase<<std::setw(16)<<std::setfill(L'0')<<hash;co_return VideoHashResult{winrt::hstring{text.str()},size};}
}
