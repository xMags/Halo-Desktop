#include "pch.h"
#include "Playback/SubtitleController.h"

#include "Api/ApiClient.h"
#include "Playback/PlaybackPolicy.h"
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

namespace
{
    constexpr wchar_t MemoryKey[]=L"halo.subtitleMemory.v1";
    constexpr std::uint32_t Chunk=64*1024;
    constexpr std::size_t Maximum=10*1024*1024;

    std::int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(),value.end(),value.begin(),[](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return value;
    }

    winrt::hstring Extension(winrt::hstring const&url)
    {
        try
        {
            auto path=std::filesystem::path(winrt::Windows::Foundation::Uri{url}.Path().c_str());
            auto const extension=Lower(path.extension().wstring());
            if(extension==L".srt"||extension==L".vtt"||extension==L".webvtt"||extension==L".ass"||extension==L".ssa"||extension==L".sub")return winrt::hstring{extension};
        }
        catch(...)
        {
        }
        return L".sub";
    }

    winrt::Windows::Data::Json::JsonObject ReadMemory()
    {
        try
        {
            auto value=winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().TryLookup(MemoryKey);
            if(value)return winrt::Windows::Data::Json::JsonObject::Parse(winrt::unbox_value<winrt::hstring>(value));
        }
        catch(...)
        {
        }
        return {};
    }

    void WriteMemory(winrt::Windows::Data::Json::JsonObject const&value)
    {
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(
            MemoryKey,winrt::box_value(value.Stringify()));
    }

    double Border(winrt::hstring const&value)
    {
        if(value==L"none")return 0;
        if(value==L"thin")return 1.5;
        if(value==L"thick")return 4.5;
        return 3;
    }

    std::wstring Font(winrt::hstring const&value)
    {
        if(value==L"Serif")return L"Georgia";
        if(value==L"Mono")return L"JetBrains Mono";
        if(value==L"System"||value.empty())return L"Segoe UI";
        return std::wstring(value.c_str());
    }
}

namespace HaloDesktop::Playback
{
    SubtitleController::SubtitleController(
        std::shared_ptr<Api::ApiClient>api,
        std::shared_ptr<IPlaybackEngine>engine,
        std::shared_ptr<Services::SettingsSyncService>settings,
        std::shared_ptr<Services::IDownloadService>downloads)
        :m_api(std::move(api)),m_engine(std::move(engine)),m_settings(std::move(settings)),m_downloads(std::move(downloads))
    {
        if(!m_api||!m_engine||!m_settings||!m_downloads)throw std::invalid_argument("SubtitleController requires dependencies.");
    }

    SubtitleController::~SubtitleController()
    {
        Stop();
        CleanupTemporaryFiles();
    }

    concurrency::task<void> SubtitleController::PrepareAsync(winrt::HaloDesktop::PlaybackRequest request)
    {
        auto const version=++m_generation;
        ++m_selectionAttempt;
        m_pendingSelectionAttempt=0;
        m_pendingSelectionDeliberate=false;
        m_request=request;
        m_choices.clear();
        m_display.clear();
        m_localSubtitlePath.reset();
        m_fileSerial=0;
        m_appliedSerial=0;
        m_appliedPreferenceRevision=0;
        m_discoveryComplete=false;
        ApplyStyle();
        SweepExternalTracks();
        CleanupTemporaryFiles();
        if(!m_engineToken)m_engineToken=m_engine->AddChangedHandler([weak=weak_from_this()](){if(auto self=weak.lock())self->OnEngineChanged();});

        if(request.IsLocalFile())
        {
            m_localSubtitlePath=m_downloads->SubtitlePath(request.DownloadId());
            m_discoveryComplete=true;
            if(m_changed)m_changed();
            OnEngineChanged();
            co_return;
        }

        auto const ui=winrt::apartment_context{};
        std::optional<winrt::hstring>hash,sizeName;
        std::optional<std::uint64_t>videoSize;
        if(!request.VideoHash().empty()&&request.VideoSize()>0)
        {
            hash=request.VideoHash();
            videoSize=request.VideoSize();
        }
        else
        {
            try
            {
                auto computed=co_await m_api->ComputeVideoHashAsync(request.Url());
                hash=computed.Hash;
                videoSize=computed.Size;
            }
            catch(...)
            {
            }
        }
        if(!request.Filename().empty())sizeName=request.Filename();

        std::optional<Api::Dto::SubtitlesPayload>payload;
        try
        {
            payload=co_await m_api->GetSubtitlesAsync(request.MediaType(),request.VideoId(),hash,videoSize,sizeName);
        }
        catch(...)
        {
        }
        co_await ui;
        if(version!=m_generation)co_return;

        std::size_t variant{};
        if(payload)
        {
            for(auto const&group:payload->Results)
            {
                for(auto const&subtitle:group.Subtitles)
                {
                    auto key=winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid());
                    auto language=SubtitleLanguageLabel(subtitle.Lang);
                    auto addon=Services::SanitizeAddonName(group.AddonName);
                    AddonSubtitleDisplay display{
                        key,
                        language,
                        addon,
                        L"Variant "+winrt::to_hstring(++variant)+(payload->HashMatched?L" · hash match":L" · name match")};
                    m_choices.emplace(std::wstring(key.c_str()),NativeChoice{display,group.AddonId,subtitle.Id,subtitle.Url,subtitle.Lang});
                    m_display.push_back(std::move(display));
                }
            }
        }
        m_discoveryComplete=true;
        if(m_changed)m_changed();
        OnEngineChanged();
    }

    concurrency::task<void> SubtitleController::SelectAsync(winrt::hstring key,bool deliberate)
    {
        auto lifetime=shared_from_this();
        auto const generation=m_generation;
        auto const attempt=++m_selectionAttempt;
        m_pendingSelectionAttempt=attempt;
        m_pendingSelectionDeliberate=deliberate;
        auto const initialSelectionSerial=m_engine->State().SubtitleSelectionSerial;
        auto const found=m_choices.find(std::wstring(key.c_str()));
        if(found==m_choices.end())
        {
            m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
            co_return;
        }
        auto const choice=found->second;
        auto const identity=std::wstring(choice.AddonId.c_str())+L":"+std::wstring(choice.SubtitleId.c_str());
        for(auto const&track:m_engine->State().Tracks)
        {
            if(track.Type==TrackType::Subtitle&&track.Identity==identity)
            {
                try{m_engine->SetSubtitleTrack(track.Id);}
                catch(...){m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;NotifyError();co_return;}
                if(!deliberate)m_autoSubtitleSelectionSerial=m_engine->State().SubtitleSelectionSerial;
                if(deliberate)Remember(choice);
                m_appliedSerial=m_fileSerial;
                m_appliedPreferenceRevision=m_preferenceRevision;
                if(m_pendingSelectionAttempt==attempt){m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;}
                co_return;
            }
        }

        auto const ui=winrt::apartment_context{};
        std::optional<std::filesystem::path>path;
        try
        {
            path=std::filesystem::path(co_await DownloadAsync(choice));
        }
        catch(...)
        {
        }
        co_await ui;
        if(generation!=m_generation||attempt!=m_selectionAttempt
            || m_engine->State().SubtitleSelectionSerial!=initialSelectionSerial)
        {
            if(path)m_temporaryFiles.Remove(*path);
            if(attempt==m_selectionAttempt){m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;}
            co_return;
        }
        if(!path)
        {
            m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
            NotifyError();
            co_return;
        }

        m_temporaryFiles.Add(*path);
        try
        {
            auto displayTitle=std::wstring(choice.Display.Language.c_str());
            if(!choice.Display.Addon.empty())displayTitle+=L" · "+std::wstring(choice.Display.Addon.c_str());
            m_engine->AddExternalSubtitle(path->wstring(),identity,displayTitle,std::wstring(choice.Lang.c_str()));
        }
        catch(...)
        {
            m_temporaryFiles.Remove(*path);
            m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
            NotifyError();
            co_return;
        }
        if(!deliberate)m_autoSubtitleSelectionSerial=m_engine->State().SubtitleSelectionSerial;
        if(deliberate)Remember(choice);
        m_appliedSerial=m_fileSerial;
        m_appliedPreferenceRevision=m_preferenceRevision;
        m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
    }

    std::vector<AddonSubtitleDisplay> SubtitleController::Choices()const{return m_display;}
    void SubtitleController::SetChoicesChangedHandler(std::function<void()>handler){m_changed=std::move(handler);if(m_changed)m_changed();}
    void SubtitleController::SetErrorHandler(std::function<void()>handler){m_error=std::move(handler);}

    void SubtitleController::RefreshPreferences()
    {
        if(!m_pendingSelectionDeliberate){++m_selectionAttempt;m_pendingSelectionAttempt=0;}
        ++m_preferenceRevision;
        ApplyStyle();
        TryApplySelection();
    }

    void SubtitleController::Stop()noexcept
    {
        ++m_generation;
        ++m_selectionAttempt;
        m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
        if(m_engineToken){m_engine->RemoveChangedHandler(m_engineToken);m_engineToken=0;}
        SweepExternalTracks();
        CleanupTemporaryFiles();
        m_changed={};
        m_error={};
        m_choices.clear();
        m_display.clear();
        m_localSubtitlePath.reset();
        m_request=nullptr;
        m_discoveryComplete=false;
    }

    void SubtitleController::CleanupTemporaryFiles()noexcept
    {
        m_temporaryFiles.Cleanup();
    }

    void SubtitleController::ApplyStyle()
    {
        m_engine->ApplySubtitleStyle({
            m_settings->SubtitleScalePercent()/100.0,
            Font(m_settings->SubtitleFontFamily()),
            Border(m_settings->SubtitleOutline()),
            m_settings->SubtitleShadow()?2.0:0.0});
    }

    void SubtitleController::SweepExternalTracks()
    {
        for(auto const&track:m_engine->State().Tracks)
        {
            if(track.Type==TrackType::Subtitle&&track.External)m_engine->RemoveTrack(track.Id);
        }
    }

    void SubtitleController::OnEngineChanged()
    {
        auto const state=m_engine->State();
        if(!m_request||state.FileSerial==0)return;
        if(state.FileSerial!=m_fileSerial)
        {
            m_fileSerial=state.FileSerial;
            m_appliedSerial=0;
            m_appliedPreferenceRevision=0;
            m_initialSubtitleSelectionSerial=state.SubtitleSelectionSerial;
            m_autoSubtitleSelectionSerial=m_initialSubtitleSelectionSerial;
            ++m_selectionAttempt;
            m_pendingSelectionAttempt=0;m_pendingSelectionDeliberate=false;
        }
        if(m_request.IsLocalFile())
        {
            if(m_appliedSerial==m_fileSerial)return;
            if(m_localSubtitlePath)
            {
                auto const language=std::wstring(m_request.SubtitleLang().c_str());
                auto const display=language.empty()?std::wstring{L"Offline subtitle"}:std::wstring(SubtitleLanguageLabel(m_request.SubtitleLang()).c_str());
                try
                {
                    m_engine->AddExternalSubtitle(m_localSubtitlePath->wstring(),L"offline:"+std::wstring(m_request.DownloadId().c_str()),display,language);
                    m_autoSubtitleSelectionSerial=m_engine->State().SubtitleSelectionSerial;
                }
                catch(...){NotifyError();}
            }
            m_appliedSerial=m_fileSerial;
            m_appliedPreferenceRevision=m_preferenceRevision;
            return;
        }
        TryApplySelection();
    }

    void SubtitleController::TryApplySelection()
    {
        if(!m_request||m_request.IsLocalFile()||!m_discoveryComplete||m_fileSerial==0||m_pendingSelectionAttempt!=0)return;
        auto const state=m_engine->State();
        if(!CanApplyAutomaticSelection(state.SubtitleSelectionSerial,m_initialSubtitleSelectionSerial,m_autoSubtitleSelectionSerial))return;
        if(m_appliedSerial==m_fileSerial&&m_appliedPreferenceRevision==m_preferenceRevision)return;

        auto applyTrack=[this,&state](std::int64_t id)
        {
            auto const selected=std::find_if(state.Tracks.begin(),state.Tracks.end(),[id](TrackInfo const&track)
            {
                return track.Type==TrackType::Subtitle&&track.Selected&&track.Id==id;
            });
            try{if(selected==state.Tracks.end())m_engine->SetSubtitleTrack(id);}
            catch(...){NotifyError();}
            m_autoSubtitleSelectionSerial=m_engine->State().SubtitleSelectionSerial;
            m_appliedSerial=m_fileSerial;
            m_appliedPreferenceRevision=m_preferenceRevision;
        };

        auto const memory=ReadSelectionMemory();
        if(memory.Identity)
        {
            auto const identity=std::wstring(memory.Identity->c_str());
            auto const existing=std::find_if(state.Tracks.begin(),state.Tracks.end(),[&identity](TrackInfo const&track)
            {
                return track.Type==TrackType::Subtitle&&track.Identity==identity;
            });
            if(existing!=state.Tracks.end()){applyTrack(existing->Id);return;}
            if(auto const key=ChoiceByIdentity(*memory.Identity)){SelectAsync(*key,false).then([](concurrency::task<void>task){try{task.get();}catch(...){}});return;}
        }

        auto language=memory.Language;
        if(!language&&!memory.Present)language=m_settings->PreferredSubtitleLanguage();
        if(language)
        {
            if(auto const track=FindLanguageTrack(state.Tracks,TrackType::Subtitle,std::wstring(language->c_str()),true)){applyTrack(*track);return;}
            if(auto const key=ChoiceByLanguage(*language)){SelectAsync(*key,false).then([](concurrency::task<void>task){try{task.get();}catch(...){}});return;}
            m_appliedSerial=m_fileSerial;
            m_appliedPreferenceRevision=m_preferenceRevision;
            return;
        }
        if(memory.Present)
        {
            m_appliedSerial=m_fileSerial;
            m_appliedPreferenceRevision=m_preferenceRevision;
            return;
        }

        auto const selected=std::any_of(state.Tracks.begin(),state.Tracks.end(),[](TrackInfo const&track)
        {
            return track.Type==TrackType::Subtitle&&track.Selected;
        });
        try{if(selected)m_engine->SetSubtitleTrack(std::nullopt);}
        catch(...){NotifyError();}
        m_autoSubtitleSelectionSerial=m_engine->State().SubtitleSelectionSerial;
        m_appliedSerial=m_fileSerial;
        m_appliedPreferenceRevision=m_preferenceRevision;
    }

    void SubtitleController::Remember(NativeChoice const&choice)
    {
        auto value=ReadMemory();
        for(auto const&key:{L"video:"+m_request.VideoId(),L"item:"+m_request.ItemId()})
        {
            winrt::Windows::Data::Json::JsonObject entry;
            entry.Insert(L"identity",winrt::Windows::Data::Json::JsonValue::CreateStringValue(choice.AddonId+L":"+choice.SubtitleId));
            entry.Insert(L"lang",winrt::Windows::Data::Json::JsonValue::CreateStringValue(choice.Lang));
            entry.Insert(L"updatedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(NowMs())));
            value.Insert(key,entry);
        }
        if(value.Size()>300)
        {
            std::vector<std::pair<winrt::hstring,double>>rows;
            for(auto const&pair:value)
            {
                if(pair.Value().ValueType()==winrt::Windows::Data::Json::JsonValueType::Object)rows.emplace_back(pair.Key(),pair.Value().GetObject().GetNamedNumber(L"updatedAt",0));
            }
            std::sort(rows.begin(),rows.end(),[](auto const&left,auto const&right){return left.second<right.second;});
            for(std::size_t index=0;index<rows.size()&&value.Size()>300;++index)value.Remove(rows[index].first);
        }
        WriteMemory(value);
    }

    SubtitleController::SelectionMemory SubtitleController::ReadSelectionMemory()const
    {
        SelectionMemory result;
        try
        {
            auto const value=ReadMemory();
            auto const video=L"video:"+m_request.VideoId();
            if(value.HasKey(video))
            {
                auto const entry=value.GetNamedObject(video);
                auto const identity=entry.GetNamedString(L"identity",L"");
                auto const language=entry.GetNamedString(L"lang",L"");
                if(!identity.empty())result.Identity=identity;
                if(!language.empty())result.Language=language;
                result.Present=true;
                return result;
            }
            auto const item=L"item:"+m_request.ItemId();
            if(value.HasKey(item))
            {
                auto const language=value.GetNamedObject(item).GetNamedString(L"lang",L"");
                if(!language.empty())result.Language=language;
                result.Present=true;
            }
        }
        catch(...)
        {
        }
        return result;
    }

    std::optional<winrt::hstring> SubtitleController::ChoiceByIdentity(winrt::hstring const&identity)const
    {
        for(auto const&[key,choice]:m_choices)
        {
            if(choice.AddonId+L":"+choice.SubtitleId==identity)return choice.Display.Key;
        }
        return std::nullopt;
    }

    std::optional<winrt::hstring> SubtitleController::ChoiceByLanguage(winrt::hstring const&language)const
    {
        for(auto const&[key,choice]:m_choices)
        {
            if(LanguageMatches(std::wstring(choice.Lang.c_str()),std::wstring(language.c_str())))return choice.Display.Key;
        }
        return std::nullopt;
    }

    concurrency::task<std::wstring> SubtitleController::DownloadAsync(NativeChoice const&choice)
    {
        auto response=co_await m_api->OpenAddonProxyAsync(choice.Url);
        auto const length=response.Content().Headers().ContentLength();
        if(length&&length.Value()>Maximum)throw std::runtime_error("Subtitle file exceeds the size limit.");
        auto input=co_await response.Content().ReadAsInputStreamAsync();
        winrt::Windows::Storage::Streams::DataReader reader{input};
        reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);
        std::vector<std::uint8_t>bytes;
        for(;;)
        {
            auto const loaded=co_await reader.LoadAsync(Chunk);
            if(!loaded)break;
            if(bytes.size()+loaded>Maximum)throw std::runtime_error("Subtitle file exceeds the size limit.");
            auto const offset=bytes.size();
            bytes.resize(offset+loaded);
            reader.ReadBytes(winrt::array_view<std::uint8_t>{bytes.data()+offset,bytes.data()+offset+loaded});
        }
        auto const folder=std::filesystem::path(winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str())/L"halo-subtitles";
        std::filesystem::create_directories(folder);
        auto const path=folder/(std::wstring(winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid()).c_str())+std::wstring(Extension(choice.Url).c_str()));
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<char const*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!output)
        {
            output.close();std::error_code error;std::filesystem::remove(path,error);
            throw std::runtime_error("Subtitle file could not be stored.");
        }
        co_return path.wstring();
    }

    void SubtitleController::NotifyError()noexcept
    {
        try{if(m_error)m_error();}catch(...){}
    }

    winrt::hstring SubtitleLanguageLabel(winrt::hstring code)
    {
        return winrt::hstring{LanguageDisplayName(std::wstring(code.c_str()))};
    }
}
