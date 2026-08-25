#include "Playback/PlaybackPolicy.h"
#include "Playback/TemporaryFileCollection.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition,char const* message)
    {
        if(!condition)throw std::runtime_error(message);
    }

    void RequireInvalid(std::function<void()> const& action,char const* message)
    {
        try
        {
            action();
        }
        catch(std::invalid_argument const&)
        {
            return;
        }
        throw std::runtime_error(message);
    }

    void TestLanguages()
    {
        Require(HaloDesktop::Playback::NormalizeLanguage(L"EN-us")==L"eng","regional English was not normalized");
        Require(HaloDesktop::Playback::LanguageDisplayName(L"fre")==L"French","language display alias was not normalized");
        Require(HaloDesktop::Playback::LanguageMatches(L"ger",L"deu"),"bibliographic German did not match terminologic German");
        std::vector<HaloDesktop::Playback::TrackInfo> tracks{
            {1,HaloDesktop::Playback::TrackType::Subtitle,L"English",L"",L"ASS",false,false,L"eng"},
            {2,HaloDesktop::Playback::TrackType::Subtitle,L"Japanese addon",L"",L"SRT",false,true,L"jpn",L"addon:id"},
            {3,HaloDesktop::Playback::TrackType::Subtitle,L"Japanese",L"",L"PGS",true,false,L"jpn"},
        };
        Require(HaloDesktop::Playback::FindLanguageTrack(tracks,HaloDesktop::Playback::TrackType::Subtitle,L"jpn",true)==3,"embedded language match did not win");
        Require(HaloDesktop::Playback::TrackSummary(tracks,HaloDesktop::Playback::TrackType::Subtitle)==L"Japanese · PGS","selected subtitle summary was not truthful");
        tracks[2].Selected=false;
        Require(HaloDesktop::Playback::TrackSummary(tracks,HaloDesktop::Playback::TrackType::Subtitle)==L"Off","disabled subtitles were not reported as off");
        auto const encoded=HaloDesktop::Playback::EncodeExternalSubtitleTrackTitle(L"private-addon:id",L"Japanese · Provider");
        auto const decoded=HaloDesktop::Playback::DecodeExternalSubtitleTrackTitle(encoded);
        Require(decoded&&decoded->first==L"private-addon:id"&&decoded->second==L"Japanese · Provider","external subtitle identity did not stay separate from its display title");
        Require(!HaloDesktop::Playback::DecodeExternalSubtitleTrackTitle(L"Japanese · Provider"),"ordinary track title was treated as an encoded identity");
        Require(HaloDesktop::Playback::CanApplyAutomaticSelection(4,4,4),"initial automatic selection was rejected");
        Require(HaloDesktop::Playback::CanApplyAutomaticSelection(5,4,5),"latest automatic selection was rejected");
        Require(!HaloDesktop::Playback::CanApplyAutomaticSelection(6,4,5),"newer manual selection did not supersede automatic selection");
    }

    void TestResume()
    {
        using HaloDesktop::Playback::ShouldApplyResume;
        Require(ShouldApplyResume(true,false,180.0,1200.0,2.0,4,4,true),"eligible resume was rejected");
        Require(!ShouldApplyResume(true,true,180.0,1200.0,2.0,4,4,true),"watched media resumed");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,6.0,4,4,true),"late playback position was rewound");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,2.0,5,4,true),"a user seek was overwritten");
        Require(!ShouldApplyResume(true,false,1150.0,1200.0,2.0,4,4,true),"near-complete media resumed");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,2.0,4,4,false),"expired startup resume was applied");
    }

    void TestHeaders()
    {
        auto const serialized=HaloDesktop::Playback::SerializePlaybackHeaders({{L"X-Test",L"a,b\\c"},{L"Authorization",L"Bearer test"}});
        Require(serialized==L"X-Test: a\\,b\\\\c,Authorization: Bearer test","header list escaping changed");
        RequireInvalid([]{HaloDesktop::Playback::ValidatePlaybackHeaders({{L"Range",L"bytes=0-1"}});},"caller range header was accepted");
        RequireInvalid([]{HaloDesktop::Playback::ValidatePlaybackHeaders({{L"Keep-Alive",L"timeout=30"}});},"hop-by-hop header was accepted");
        RequireInvalid([]{HaloDesktop::Playback::ValidatePlaybackHeaders({{L"X-Test",L"safe\r\ninjected"}});},"header injection was accepted");
        RequireInvalid([]{HaloDesktop::Playback::ValidatePlaybackHeaders({{L"Bad Header",L"value"}});},"invalid header name was accepted");
    }

    void TestTemporaryFileCleanup()
    {
        auto const suffix=std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        auto const root=std::filesystem::temp_directory_path()/(L"halo-playback-tests-"+suffix);
        Require(std::filesystem::create_directory(root),"temporary test directory could not be created");
        struct DirectoryCleanup final
        {
            std::filesystem::path Path;
            ~DirectoryCleanup(){std::error_code error;std::filesystem::remove_all(Path,error);}
        };
        [[maybe_unused]] DirectoryCleanup cleanup{root};
        auto const first=root/L"first.srt";auto const second=root/L"second.ass";
        std::ofstream{first}<<"first";std::ofstream{second}<<"second";
        Require(std::filesystem::is_regular_file(first)&&std::filesystem::is_regular_file(second),"temporary subtitle fixtures were not created");
        HaloDesktop::Playback::TemporaryFileCollection files;files.Add(first);files.Add(second);
        Require(files.Size()==2,"temporary subtitle ownership was not recorded");
        files.Cleanup();
        Require(files.Size()==0,"temporary subtitle ownership remained after cleanup");
        Require(!std::filesystem::exists(first)&&!std::filesystem::exists(second),"temporary subtitle files remained after cleanup");
    }
}

int main()
{
    try
    {
        TestLanguages();
        TestResume();
        TestHeaders();
        TestTemporaryFileCleanup();
        std::cout<<"Playback policy tests passed.\n";
        return 0;
    }
    catch(std::exception const&error)
    {
        std::cerr<<"Playback policy tests failed: "<<error.what()<<'\n';
        return 1;
    }
}
