#include "Playback/MpvCommand.h"

#include "Playback/PlaybackPolicy.h"

#include <mpv/client.h>

#include <array>
#include <climits>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    std::string Utf8(std::wstring const& value)
    {
        if(value.empty())return {};
        if(value.size()>static_cast<std::size_t>(INT_MAX))throw std::length_error("Playback text is too large.");
        auto const size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if(size<=0)throw std::invalid_argument("Playback text is not valid Unicode.");
        std::string result(static_cast<std::size_t>(size),'\0');
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr)!=size)
        {
            throw std::invalid_argument("Playback text could not be encoded.");
        }
        return result;
    }

    void CheckMpv(char const* operation,int result)
    {
        if(result>=0)return;
        auto const description=mpv_error_string(result);
        throw std::runtime_error(std::string(operation)+": "+(description?description:"unknown libmpv error"));
    }

    void Command(mpv_handle*handle,std::vector<std::string> const&arguments)
    {
        if(!handle)throw std::invalid_argument("An initialized mpv handle is required.");
        std::vector<char const*>values;values.reserve(arguments.size()+1);
        for(auto const&argument:arguments)values.push_back(argument.c_str());
        values.push_back(nullptr);
        CheckMpv("mpv command",mpv_command(handle,values.data()));
    }
}

namespace HaloDesktop::Playback
{
    void LoadMpvSource(mpv_handle*handle,PlaybackSource const&source)
    {
        if(!handle)throw std::invalid_argument("An initialized mpv handle is required.");
        auto sourceUtf8=Utf8(source.Location);
        auto headerFields=Utf8(SerializePlaybackHeaders(source.Headers));
        if(headerFields.empty())
        {
            Command(handle,{"loadfile",std::move(sourceUtf8),"replace"});
            return;
        }

        std::string commandName{"loadfile"};std::string replace{"replace"};std::string optionName{"http-header-fields"};
        std::int64_t insertionIndex=-1;
        std::array<mpv_node,1>optionValues{};optionValues[0].format=MPV_FORMAT_STRING;optionValues[0].u.string=headerFields.data();
        std::array<char*,1>optionKeys{optionName.data()};
        mpv_node_list optionList{.num=1,.values=optionValues.data(),.keys=optionKeys.data()};
        std::array<mpv_node,5>commandValues{};
        commandValues[0].format=MPV_FORMAT_STRING;commandValues[0].u.string=commandName.data();
        commandValues[1].format=MPV_FORMAT_STRING;commandValues[1].u.string=sourceUtf8.data();
        commandValues[2].format=MPV_FORMAT_STRING;commandValues[2].u.string=replace.data();
        commandValues[3].format=MPV_FORMAT_INT64;commandValues[3].u.int64=insertionIndex;
        commandValues[4].format=MPV_FORMAT_NODE_MAP;commandValues[4].u.list=&optionList;
        mpv_node_list commandList{.num=static_cast<int>(commandValues.size()),.values=commandValues.data(),.keys=nullptr};
        mpv_node command{};command.format=MPV_FORMAT_NODE_ARRAY;command.u.list=&commandList;
        mpv_node result{};
        auto const code=mpv_command_node(handle,&command,&result);
        mpv_free_node_contents(&result);
        CheckMpv("mpv loadfile",code);
    }
}
