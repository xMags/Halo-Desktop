#include "pch.h"
#include "Playback/SubtitleController.h"

#include "Api/ApiClient.h"
#include "Services/SettingsSyncService.h"
#include "Services/ServiceInterfaces.h"
#include "Services/SourceService.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <wil/resource.h>

namespace
{
    constexpr wchar_t MemoryKey[]=L"halo.subtitleMemory.v1";constexpr std::uint32_t Chunk=64*1024;constexpr std::size_t Maximum=10*1024*1024;
    std::int64_t NowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
    std::wstring Lower(std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return value;}
    winrt::hstring Extension(winrt::hstring const&url){try{auto path=std::filesystem::path(winrt::Windows::Foundation::Uri{url}.Path().c_str());auto ext=Lower(path.extension().wstring());if(ext==L".srt"||ext==L".vtt"||ext==L".webvtt"||ext==L".ass"||ext==L".ssa"||ext==L".sub")return winrt::hstring{ext};}catch(...){}return L".sub";}
    winrt::Windows::Data::Json::JsonObject ReadMemory(){try{auto value=winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().TryLookup(MemoryKey);if(value)return winrt::Windows::Data::Json::JsonObject::Parse(winrt::unbox_value<winrt::hstring>(value));}catch(...){}return {};}
    void WriteMemory(winrt::Windows::Data::Json::JsonObject const&value){winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(MemoryKey,winrt::box_value(value.Stringify()));}
    double Border(winrt::hstring const&value){if(value==L"none")return 0;if(value==L"thin")return 1.5;if(value==L"thick")return 4.5;return 3;}
    std::wstring Font(winrt::hstring const&value){if(value==L"Serif")return L"Georgia";if(value==L"Mono")return L"JetBrains Mono";if(value==L"System"||value.empty())return L"Segoe UI";return std::wstring(value.c_str());}
}

namespace HaloDesktop::Playback
{
    SubtitleController::SubtitleController(
        std::shared_ptr<Api::ApiClient>api,
        std::shared_ptr<IPlaybackEngine>engine,
        std::shared_ptr<Services::SettingsSyncService>settings,
        std::shared_ptr<Services::IDownloadService> downloads)
        :m_api(std::move(api)),m_engine(std::move(engine)),m_settings(std::move(settings)),m_downloads(std::move(downloads))
    {if(!m_api||!m_engine||!m_settings||!m_downloads)throw std::invalid_argument("SubtitleController requires dependencies.");}
    SubtitleController::~SubtitleController(){Stop();}
    concurrency::task<void> SubtitleController::PrepareAsync(winrt::HaloDesktop::PlaybackRequest request)
    {
        auto const version=++m_generation;auto const ui=winrt::apartment_context{};m_request=request;m_choices.clear();m_display.clear();m_localSubtitlePath.reset();m_appliedSerial=0;
        try{co_await m_settings->LoadAsync();}catch(...){}co_await ui;if(version!=m_generation)co_return;ApplyStyle();SweepExternalTracks();
        if(request.IsLocalFile())
        {
            m_localSubtitlePath=m_downloads->SubtitlePath(request.DownloadId());
            if(m_changed)m_changed();
            if(!m_engineToken)m_engineToken=m_engine->AddChangedHandler([weak=weak_from_this()](){if(auto self=weak.lock())self->OnEngineChanged();});
            OnEngineChanged();
            co_return;
        }
        std::optional<winrt::hstring>hash,sizeName;std::optional<std::uint64_t>videoSize;if(!request.VideoHash().empty()&&request.VideoSize()>0){hash=request.VideoHash();videoSize=request.VideoSize();}else{try{auto computed=co_await m_api->ComputeVideoHashAsync(request.Url());hash=computed.Hash;videoSize=computed.Size;}catch(...){}}if(!request.Filename().empty())sizeName=request.Filename();
        auto payload=co_await m_api->GetSubtitlesAsync(request.MediaType(),request.VideoId(),hash,videoSize,sizeName);co_await ui;if(version!=m_generation)co_return;
        std::size_t variant{};for(auto const&group:payload.Results)for(auto const&subtitle:group.Subtitles){auto key=winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid());auto language=SubtitleLanguageLabel(subtitle.Lang);auto addon=Services::SanitizeAddonName(group.AddonName);AddonSubtitleDisplay display{key,language,addon,L"Variant "+winrt::to_hstring(++variant)+(payload.HashMatched?L" · hash match":L" · name match")};m_choices.emplace(std::wstring(key.c_str()),NativeChoice{display,group.AddonId,subtitle.Id,subtitle.Url,subtitle.Lang});m_display.push_back(display);}
        if(m_changed)m_changed();if(!m_engineToken)m_engineToken=m_engine->AddChangedHandler([weak=weak_from_this()](){if(auto self=weak.lock())self->OnEngineChanged();});OnEngineChanged();
    }
    concurrency::task<void> SubtitleController::SelectAsync(winrt::hstring key,bool deliberate)
    {
        auto lifetime=shared_from_this();auto const generation=m_generation;auto found=m_choices.find(std::wstring(key.c_str()));if(found==m_choices.end()||m_selecting)co_return;auto choice=found->second;auto identity=std::wstring(choice.AddonId.c_str())+L":"+std::wstring(choice.SubtitleId.c_str());
        for(auto const&track:m_engine->State().Tracks)if(track.Type==TrackType::Subtitle&&track.Title==identity){m_engine->SetSubtitleTrack(track.Id);if(deliberate)Remember(choice);co_return;}
        m_selecting=true;auto reset=wil::scope_exit([this](){m_selecting=false;});auto const path=co_await DownloadAsync(choice);if(generation!=m_generation)co_return;m_engine->AddExternalSubtitle(path,identity);if(deliberate)Remember(choice);
    }
    std::vector<AddonSubtitleDisplay>SubtitleController::Choices()const{return m_display;}
    void SubtitleController::SetChoicesChangedHandler(std::function<void()>handler){m_changed=std::move(handler);if(m_changed)m_changed();}
    void SubtitleController::Stop()noexcept{++m_generation;if(m_engineToken){m_engine->RemoveChangedHandler(m_engineToken);m_engineToken=0;}m_changed={};m_choices.clear();m_display.clear();m_localSubtitlePath.reset();m_request=nullptr;}
    void SubtitleController::ApplyStyle(){m_engine->ApplySubtitleStyle({m_settings->SubtitleScalePercent()/100.0,Font(m_settings->SubtitleFontFamily()),Border(m_settings->SubtitleOutline()),m_settings->SubtitleShadow()?2.0:0.0});}
    void SubtitleController::SweepExternalTracks(){for(auto const&track:m_engine->State().Tracks)if(track.Type==TrackType::Subtitle&&track.External)m_engine->RemoveTrack(track.Id);}
    void SubtitleController::OnEngineChanged(){auto const state=m_engine->State();if(!m_request||state.FileSerial==0||state.FileSerial==m_appliedSerial)return;m_appliedSerial=state.FileSerial;if(m_request.IsLocalFile()){if(m_localSubtitlePath)m_engine->AddExternalSubtitle(m_localSubtitlePath->wstring(),L"offline:"+std::wstring{m_request.DownloadId().c_str()});return;}ApplyRememberedOrDefault();}
    void SubtitleController::ApplyRememberedOrDefault(){auto key=PreferredKey();if(key){SelectAsync(*key,false).then([](concurrency::task<void>task){try{task.get();}catch(...){}});return;}m_engine->SetSubtitleTrack(std::nullopt);}
    void SubtitleController::Remember(NativeChoice const&choice){auto value=ReadMemory();for(auto const&key:{L"video:"+m_request.VideoId(),L"item:"+m_request.ItemId()}){winrt::Windows::Data::Json::JsonObject entry;entry.Insert(L"identity",winrt::Windows::Data::Json::JsonValue::CreateStringValue(choice.AddonId+L":"+choice.SubtitleId));entry.Insert(L"lang",winrt::Windows::Data::Json::JsonValue::CreateStringValue(choice.Lang));entry.Insert(L"updatedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(NowMs())));value.Insert(key,entry);}if(value.Size()>300){std::vector<std::pair<winrt::hstring,double>>rows;for(auto const&pair:value){if(pair.Value().ValueType()==winrt::Windows::Data::Json::JsonValueType::Object)rows.emplace_back(pair.Key(),pair.Value().GetObject().GetNamedNumber(L"updatedAt",0));}std::sort(rows.begin(),rows.end(),[](auto const&a,auto const&b){return a.second<b.second;});for(std::size_t i=0;i<rows.size()&&value.Size()>300;++i)value.Remove(rows[i].first);}WriteMemory(value);}
    std::optional<winrt::hstring>SubtitleController::PreferredKey()const{auto value=ReadMemory();auto matchIdentity=[&](winrt::hstring const&identity)->std::optional<winrt::hstring>{for(auto const&[key,choice]:m_choices)if(choice.AddonId+L":"+choice.SubtitleId==identity)return choice.Display.Key;return std::nullopt;};auto matchLang=[&](winrt::hstring const&lang)->std::optional<winrt::hstring>{for(auto const&[key,choice]:m_choices)if(Lower(std::wstring(choice.Lang.c_str()))==Lower(std::wstring(lang.c_str())))return choice.Display.Key;return std::nullopt;};try{auto exact=L"video:"+m_request.VideoId();if(value.HasKey(exact)){auto entry=value.GetNamedObject(exact);if(auto result=matchIdentity(entry.GetNamedString(L"identity",L"")))return result;}auto item=L"item:"+m_request.ItemId();if(value.HasKey(item)){auto entry=value.GetNamedObject(item);if(auto result=matchLang(entry.GetNamedString(L"lang",L"")))return result;}}catch(...){}auto preferred=m_settings->PreferredSubtitleLanguage();return preferred?matchLang(*preferred):std::nullopt;}
    concurrency::task<std::wstring>SubtitleController::DownloadAsync(NativeChoice const&choice){auto response=co_await m_api->OpenAddonProxyAsync(choice.Url);auto length=response.Content().Headers().ContentLength();if(length&&length.Value()>Maximum)throw std::runtime_error("Subtitle file exceeds the size limit.");auto input=co_await response.Content().ReadAsInputStreamAsync();winrt::Windows::Storage::Streams::DataReader reader{input};reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);std::vector<std::uint8_t>bytes;for(;;){auto loaded=co_await reader.LoadAsync(Chunk);if(!loaded)break;if(bytes.size()+loaded>Maximum)throw std::runtime_error("Subtitle file exceeds the size limit.");auto offset=bytes.size();bytes.resize(offset+loaded);reader.ReadBytes(winrt::array_view<std::uint8_t>{bytes.data()+offset,bytes.data()+offset+loaded});}auto folder=std::filesystem::path(winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str())/L"halo-subtitles";std::filesystem::create_directories(folder);auto path=folder/(std::wstring(winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid()).c_str())+std::wstring(Extension(choice.Url).c_str()));std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<char const*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!output)throw std::runtime_error("Subtitle file could not be stored.");co_return path.wstring();}
    winrt::hstring SubtitleLanguageLabel(winrt::hstring code){auto value=Lower(std::wstring(code.c_str()));static std::unordered_map<std::wstring,wchar_t const*>const labels{{L"eng",L"English"},{L"jpn",L"Japanese"},{L"spa",L"Spanish"},{L"fre",L"French"},{L"fra",L"French"},{L"ger",L"German"},{L"deu",L"German"},{L"por",L"Portuguese"},{L"pob",L"Portuguese (BR)"},{L"ita",L"Italian"},{L"rus",L"Russian"},{L"chi",L"Chinese"},{L"zho",L"Chinese"}};auto found=labels.find(value);return found==labels.end()?code:winrt::hstring{found->second};}
}
