#include "Playback/MpvCommand.h"
#include "Playback/PlaybackPolicy.h"

#include <mpv/client.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    void Require(bool condition,char const*message)
    {
        if(!condition)throw std::runtime_error(message);
    }

    void SendAll(SOCKET socket,char const*data,std::size_t size)
    {
        std::size_t sent{};
        while(sent<size)
        {
            auto const count=send(socket,data+sent,static_cast<int>((std::min)(size-sent,static_cast<std::size_t>(INT_MAX))),0);
            if(count<=0)throw std::runtime_error("The test HTTP response could not be sent.");
            sent+=static_cast<std::size_t>(count);
        }
    }

    void Append16(std::vector<std::uint8_t>&value,std::uint16_t number)
    {
        value.push_back(static_cast<std::uint8_t>(number));
        value.push_back(static_cast<std::uint8_t>(number>>8));
    }

    void Append32(std::vector<std::uint8_t>&value,std::uint32_t number)
    {
        value.push_back(static_cast<std::uint8_t>(number));
        value.push_back(static_cast<std::uint8_t>(number>>8));
        value.push_back(static_cast<std::uint8_t>(number>>16));
        value.push_back(static_cast<std::uint8_t>(number>>24));
    }

    void AppendText(std::vector<std::uint8_t>&value,char const*text)
    {
        for(;*text!='\0';++text)value.push_back(static_cast<std::uint8_t>(*text));
    }

    std::vector<std::uint8_t> SilentWave()
    {
        constexpr std::uint32_t sampleRate=8000;
        constexpr std::uint16_t channels=1;
        constexpr std::uint16_t bitsPerSample=16;
        constexpr std::uint32_t samples=sampleRate/4;
        constexpr std::uint32_t dataBytes=samples*channels*bitsPerSample/8;
        std::vector<std::uint8_t>result;result.reserve(44+dataBytes);
        AppendText(result,"RIFF");Append32(result,36+dataBytes);AppendText(result,"WAVEfmt ");Append32(result,16);
        Append16(result,1);Append16(result,channels);Append32(result,sampleRate);Append32(result,sampleRate*channels*bitsPerSample/8);
        Append16(result,channels*bitsPerSample/8);Append16(result,bitsPerSample);AppendText(result,"data");Append32(result,dataBytes);
        result.resize(44+dataBytes,0);return result;
    }

    std::vector<std::uint8_t> StreamingWaveHeader(std::uint32_t dataBytes)
    {
        constexpr std::uint32_t sampleRate=8000;
        constexpr std::uint16_t channels=1;
        constexpr std::uint16_t bitsPerSample=16;
        std::vector<std::uint8_t>result;result.reserve(44);
        AppendText(result,"RIFF");Append32(result,36+dataBytes);AppendText(result,"WAVEfmt ");Append32(result,16);
        Append16(result,1);Append16(result,channels);Append32(result,sampleRate);Append32(result,sampleRate*channels*bitsPerSample/8);
        Append16(result,channels*bitsPerSample/8);Append16(result,bitsPerSample);AppendText(result,"data");Append32(result,dataBytes);
        return result;
    }

    class ProtectedHttpServer final
    {
    public:
        ProtectedHttpServer()
        {
            WSADATA data{};
            if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("Winsock could not start.");
            m_socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(m_socket==INVALID_SOCKET){WSACleanup();throw std::runtime_error("The test HTTP socket could not be created.");}
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
            if(bind(m_socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR
                || listen(m_socket,SOMAXCONN)==SOCKET_ERROR)
            {
                closesocket(m_socket);WSACleanup();throw std::runtime_error("The test HTTP server could not listen.");
            }
            int addressSize=sizeof(address);
            if(getsockname(m_socket,reinterpret_cast<sockaddr*>(&address),&addressSize)==SOCKET_ERROR)
            {
                closesocket(m_socket);WSACleanup();throw std::runtime_error("The test HTTP port could not be read.");
            }
            m_port=ntohs(address.sin_port);
            m_thread=std::jthread([this]{Run();});
        }

        ~ProtectedHttpServer()
        {
            m_stopping.store(true);
            if(m_socket!=INVALID_SOCKET){closesocket(m_socket);m_socket=INVALID_SOCKET;}
            if(m_thread.joinable())m_thread.join();
            WSACleanup();
        }

        ProtectedHttpServer(ProtectedHttpServer const&)=delete;
        ProtectedHttpServer&operator=(ProtectedHttpServer const&)=delete;

        [[nodiscard]] std::wstring Url()const{return L"http://127.0.0.1:"+std::to_wstring(m_port)+L"/protected.wav";}
        [[nodiscard]] std::wstring PublicUrl()const{return L"http://127.0.0.1:"+std::to_wstring(m_port)+L"/public.wav";}
        [[nodiscard]] std::wstring SlowUrl()const{return L"http://127.0.0.1:"+std::to_wstring(m_port)+L"/slow.wav";}
        [[nodiscard]] int AuthorizedRequests()const noexcept{return m_authorized.load();}
        [[nodiscard]] int RejectedRequests()const noexcept{return m_rejected.load();}
        [[nodiscard]] int PublicRequests()const noexcept{return m_public.load();}
        [[nodiscard]] int LeakedRequests()const noexcept{return m_leaked.load();}
        [[nodiscard]] int SlowRequests()const noexcept{return m_slow.load();}

    private:
        void Run()noexcept
        {
            while(!m_stopping.load())
            {
                auto const client=accept(m_socket,nullptr,nullptr);
                if(client==INVALID_SOCKET)continue;
                try{Serve(client);}catch(...){ }
                shutdown(client,SD_BOTH);closesocket(client);
            }
        }

        void Serve(SOCKET client)
        {
            std::string request;std::array<char,2048>buffer{};
            while(request.find("\r\n\r\n")==std::string::npos&&request.size()<64*1024)
            {
                auto const count=recv(client,buffer.data(),static_cast<int>(buffer.size()),0);
                if(count<=0)break;
                request.append(buffer.data(),static_cast<std::size_t>(count));
            }
            auto const protectedHeader=request.find("\r\nX-Halo-Test: integration-value\r\n")!=std::string::npos;
            auto const publicRequest=request.starts_with("GET /public.wav ");
            auto const slowRequest=request.starts_with("GET /slow.wav ");
            auto const unprotectedRequest=publicRequest||slowRequest;
            if(unprotectedRequest&&protectedHeader)
            {
                ++m_leaked;
                constexpr char response[]="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                SendAll(client,response,sizeof(response)-1);return;
            }
            if(!unprotectedRequest&&!protectedHeader)
            {
                ++m_rejected;
                constexpr char response[]="HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                SendAll(client,response,sizeof(response)-1);return;
            }
            if(slowRequest)
            {
                ++m_slow;
                constexpr std::uint32_t dataBytes=32u*1024u*1024u;
                auto const waveHeader=StreamingWaveHeader(dataBytes);
                auto const header="HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: "+std::to_string(waveHeader.size()+dataBytes)+"\r\nConnection: close\r\n\r\n";
                SendAll(client,header.data(),header.size());
                SendAll(client,reinterpret_cast<char const*>(waveHeader.data()),waveHeader.size());
                std::array<std::uint8_t,4096>silence{};
                while(!m_stopping.load())
                {
                    SendAll(client,reinterpret_cast<char const*>(silence.data()),silence.size());
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return;
            }
            if(publicRequest)++m_public;else ++m_authorized;
            auto const body=SilentWave();
            auto const header="HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n";
            SendAll(client,header.data(),header.size());
            SendAll(client,reinterpret_cast<char const*>(body.data()),body.size());
        }

        SOCKET m_socket{INVALID_SOCKET};
        std::uint16_t m_port{};
        std::atomic_bool m_stopping{};
        std::atomic_int m_authorized{};
        std::atomic_int m_rejected{};
        std::atomic_int m_public{};
        std::atomic_int m_leaked{};
        std::atomic_int m_slow{};
        std::jthread m_thread;
    };

    void CheckMpv(int result,char const*operation)
    {
        if(result>=0)return;
        auto const description=mpv_error_string(result);
        throw std::runtime_error(std::string(operation)+": "+(description?description:"unknown mpv error"));
    }

    void WaitForSuccessfulPlayback(mpv_handle*handle,int&cachePauseUpdates)
    {
        auto const deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        bool loaded{},playbackRestarted{};
        while(std::chrono::steady_clock::now()<deadline)
        {
            auto const event=mpv_wait_event(handle,0.25);
            if(!event)continue;
            if(event->event_id==MPV_EVENT_FILE_LOADED)loaded=true;
            if(event->event_id==MPV_EVENT_PLAYBACK_RESTART)playbackRestarted=true;
            if(event->event_id==MPV_EVENT_PROPERTY_CHANGE)
            {
                auto const property=static_cast<mpv_event_property const*>(event->data);
                if(property&&property->name&&std::string_view{property->name}=="paused-for-cache"
                    &&property->format==MPV_FORMAT_FLAG&&property->data)
                {
                    ++cachePauseUpdates;
                }
            }
            if(event->event_id!=MPV_EVENT_END_FILE)continue;
            auto const end=static_cast<mpv_event_end_file const*>(event->data);
            Require(end&&end->reason==MPV_END_FILE_REASON_EOF,"mpv did not reach EOF successfully");
            Require(loaded,"mpv reached EOF before reporting FileLoaded");
            Require(playbackRestarted,"mpv reached EOF before reporting PlaybackRestart");
            return;
        }
        throw std::runtime_error("mpv playback timed out.");
    }

    void VerifyStreamingShutdown(ProtectedHttpServer&server)
    {
        auto*handle=mpv_create();Require(handle!=nullptr,"shutdown probe mpv_create returned null");
        std::atomic_bool stopping{},loaded{},playbackRestarted{};
        std::jthread eventThread;
        try
        {
            CheckMpv(mpv_set_option_string(handle,"vo","null"),"shutdown probe set vo");
            CheckMpv(mpv_set_option_string(handle,"ao","null"),"shutdown probe set ao");
            CheckMpv(mpv_set_option_string(handle,"idle","yes"),"shutdown probe set idle");
            CheckMpv(mpv_set_option_string(handle,"terminal","no"),"shutdown probe disable terminal");
            CheckMpv(mpv_initialize(handle),"shutdown probe mpv_initialize");
            eventThread=std::jthread([handle,&stopping,&loaded,&playbackRestarted]
            {
                for(;;)
                {
                    auto const event=mpv_wait_event(handle,-1.0);
                    if(!event||HaloDesktop::Playback::ShouldExitMpvEventLoop(
                        stopping.load(),
                        event->event_id==MPV_EVENT_SHUTDOWN))return;
                    if(event->event_id==MPV_EVENT_FILE_LOADED)loaded.store(true);
                    if(event->event_id==MPV_EVENT_PLAYBACK_RESTART)playbackRestarted.store(true);
                }
            });
            HaloDesktop::Playback::LoadMpvSource(handle,{server.SlowUrl(),{}});
            auto const readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(std::chrono::steady_clock::now()<readyDeadline
                &&(!loaded.load()||!playbackRestarted.load()))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            auto const shutdownStarted=std::chrono::steady_clock::now();
            char const*quitArguments[]={"quit",nullptr};
            auto const quitResult=mpv_command(handle,quitArguments);
            stopping.store(true);
            mpv_wakeup(handle);
            eventThread.join();
            mpv_terminate_destroy(handle);
            handle=nullptr;
            auto const shutdownElapsed=std::chrono::steady_clock::now()-shutdownStarted;

            Require(quitResult>=0,"mpv rejected shutdown while streaming");
            Require(server.SlowRequests()>0,"the stalled HTTP stream was not requested");
            Require(loaded.load()&&playbackRestarted.load(),"the stalled HTTP stream did not begin playback");
            Require(shutdownElapsed<std::chrono::seconds(5),"mpv shutdown exceeded five seconds while streaming");
        }
        catch(...)
        {
            stopping.store(true);
            if(handle)mpv_wakeup(handle);
            if(eventThread.joinable())eventThread.join();
            if(handle)mpv_terminate_destroy(handle);
            throw;
        }
    }
}

int main()
{
    try
    {
        ProtectedHttpServer server;
        auto*handle=mpv_create();Require(handle!=nullptr,"mpv_create returned null");
        try
        {
            CheckMpv(mpv_set_option_string(handle,"vo","null"),"set vo");
            CheckMpv(mpv_set_option_string(handle,"ao","null"),"set ao");
            CheckMpv(mpv_set_option_string(handle,"idle","yes"),"set idle");
            CheckMpv(mpv_set_option_string(handle,"terminal","no"),"disable terminal");
            CheckMpv(mpv_initialize(handle),"mpv_initialize");
            CheckMpv(mpv_observe_property(handle,0,"paused-for-cache",MPV_FORMAT_FLAG),"observe cache pause");
            int cachePauseUpdates{};
            HaloDesktop::Playback::LoadMpvSource(handle,{server.Url(),{{L"X-Halo-Test",L"integration-value"}}});
            WaitForSuccessfulPlayback(handle,cachePauseUpdates);
            HaloDesktop::Playback::LoadMpvSource(handle,{server.Url(),{{L"X-Halo-Test",L"integration-value"}}});
            WaitForSuccessfulPlayback(handle,cachePauseUpdates);
            HaloDesktop::Playback::LoadMpvSource(handle,{server.PublicUrl(),{}});
            WaitForSuccessfulPlayback(handle,cachePauseUpdates);
            Require(server.AuthorizedRequests()>=2,"protected headers were not preserved for replay");
            Require(server.RejectedRequests()==0,"mpv attempted the protected source without its header");
            Require(server.PublicRequests()>=1,"the unprotected source was not requested");
            Require(server.LeakedRequests()==0,"protected headers leaked into the next source");
            Require(cachePauseUpdates>0,"mpv did not publish cache pause state changes");
        }
        catch(...)
        {
            mpv_terminate_destroy(handle);throw;
        }
        mpv_terminate_destroy(handle);
        VerifyStreamingShutdown(server);
        std::cout<<"mpv protected playback, replay, and streaming shutdown tests passed.\n";
        return 0;
    }
    catch(std::exception const&error)
    {
        std::cerr<<"mpv integration tests failed: "<<error.what()<<'\n';
        return 1;
    }
}
