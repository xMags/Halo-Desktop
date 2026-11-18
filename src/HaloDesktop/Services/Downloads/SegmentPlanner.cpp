#include "Services/Downloads/SegmentPlanner.h"

#include <algorithm>
#include <utility>

namespace
{
    namespace Segments = HaloDesktop::Services::Downloads::Segments;

    // Deeper than any sidecar this app writes, so a crafted file cannot recurse
    // the parser off the stack.
    constexpr int MaximumNesting = 16;

    void AppendUnsigned(std::string& output, std::uint64_t value)
    {
        output += std::to_string(value);
    }

    void AppendString(std::string& output, std::wstring const& value)
    {
        constexpr char Hex[] = "0123456789abcdef";
        output.push_back('"');
        for (auto const character : value)
        {
            auto const unit = static_cast<std::uint32_t>(character);
            if (unit == '"' || unit == '\\')
            {
                output.push_back('\\');
                output.push_back(static_cast<char>(unit));
            }
            else if (unit >= 0x20 && unit < 0x7f)
            {
                output.push_back(static_cast<char>(unit));
            }
            else
            {
                output += "\\u";
                output.push_back(Hex[(unit >> 12) & 0xf]);
                output.push_back(Hex[(unit >> 8) & 0xf]);
                output.push_back(Hex[(unit >> 4) & 0xf]);
                output.push_back(Hex[unit & 0xf]);
            }
        }
        output.push_back('"');
    }

    // A strict reader for the one document shape the sidecar has. Anything it
    // does not understand fails the whole decode.
    class Reader final
    {
    public:
        explicit Reader(std::string_view text) noexcept
            : m_text(text)
        {
        }

        [[nodiscard]] bool AtEnd() noexcept
        {
            SkipSpace();
            return m_offset == m_text.size();
        }

        [[nodiscard]] bool Consume(char expected) noexcept
        {
            SkipSpace();
            if (m_offset < m_text.size() && m_text[m_offset] == expected)
            {
                ++m_offset;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool Peek(char expected) noexcept
        {
            SkipSpace();
            return m_offset < m_text.size() && m_text[m_offset] == expected;
        }

        [[nodiscard]] std::optional<std::uint64_t> Unsigned() noexcept
        {
            SkipSpace();
            auto const first = m_offset;
            std::uint64_t value{};
            while (m_offset < m_text.size() && m_text[m_offset] >= '0' && m_text[m_offset] <= '9')
            {
                auto const digit = static_cast<std::uint64_t>(m_text[m_offset] - '0');
                if (value > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
                {
                    return std::nullopt;
                }
                value = value * 10 + digit;
                ++m_offset;
            }
            auto const length = m_offset - first;
            if (length == 0 || (length > 1 && m_text[first] == '0'))
            {
                return std::nullopt;
            }
            // A fraction or exponent is not an integer, whatever its value.
            if (m_offset < m_text.size()
                && (m_text[m_offset] == '.' || m_text[m_offset] == 'e' || m_text[m_offset] == 'E'))
            {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] std::optional<std::wstring> String() noexcept
        {
            if (!Consume('"'))
            {
                return std::nullopt;
            }
            std::wstring result;
            while (m_offset < m_text.size())
            {
                auto const character = static_cast<unsigned char>(m_text[m_offset++]);
                if (character == '"')
                {
                    return result;
                }
                // Only this app writes the file, and it writes ASCII.
                if (character < 0x20 || character >= 0x80)
                {
                    return std::nullopt;
                }
                if (character != '\\')
                {
                    result.push_back(static_cast<wchar_t>(character));
                    continue;
                }
                if (m_offset >= m_text.size())
                {
                    return std::nullopt;
                }
                auto const escape = m_text[m_offset++];
                switch (escape)
                {
                case '"': result.push_back(L'"'); break;
                case '\\': result.push_back(L'\\'); break;
                case '/': result.push_back(L'/'); break;
                case 'b': result.push_back(L'\b'); break;
                case 'f': result.push_back(L'\f'); break;
                case 'n': result.push_back(L'\n'); break;
                case 'r': result.push_back(L'\r'); break;
                case 't': result.push_back(L'\t'); break;
                case 'u':
                {
                    if (m_text.size() - m_offset < 4)
                    {
                        return std::nullopt;
                    }
                    std::uint32_t unit{};
                    for (int index = 0; index < 4; ++index)
                    {
                        auto const digit = m_text[m_offset++];
                        unit <<= 4;
                        if (digit >= '0' && digit <= '9') unit |= static_cast<std::uint32_t>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f') unit |= static_cast<std::uint32_t>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F') unit |= static_cast<std::uint32_t>(digit - 'A' + 10);
                        else return std::nullopt;
                    }
                    result.push_back(static_cast<wchar_t>(unit));
                    break;
                }
                default:
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        // Steps over a value this reader has no field for.
        [[nodiscard]] bool SkipValue(int depth) noexcept
        {
            if (depth > MaximumNesting)
            {
                return false;
            }
            SkipSpace();
            if (m_offset >= m_text.size())
            {
                return false;
            }
            auto const character = m_text[m_offset];
            if (character == '"')
            {
                return String().has_value();
            }
            if (character == '{')
            {
                ++m_offset;
                if (Consume('}')) return true;
                do
                {
                    if (!String() || !Consume(':') || !SkipValue(depth + 1)) return false;
                } while (Consume(','));
                return Consume('}');
            }
            if (character == '[')
            {
                ++m_offset;
                if (Consume(']')) return true;
                do
                {
                    if (!SkipValue(depth + 1)) return false;
                } while (Consume(','));
                return Consume(']');
            }
            for (std::string_view const word : { std::string_view{ "true" }, std::string_view{ "false" }, std::string_view{ "null" } })
            {
                if (m_text.substr(m_offset, word.size()) == word)
                {
                    m_offset += word.size();
                    return true;
                }
            }
            if (character == '-' || (character >= '0' && character <= '9'))
            {
                ++m_offset;
                while (m_offset < m_text.size())
                {
                    auto const next = m_text[m_offset];
                    if ((next >= '0' && next <= '9') || next == '.' || next == 'e' || next == 'E'
                        || next == '+' || next == '-')
                    {
                        ++m_offset;
                        continue;
                    }
                    break;
                }
                return true;
            }
            return false;
        }

    private:
        void SkipSpace() noexcept
        {
            while (m_offset < m_text.size()
                && (m_text[m_offset] == ' ' || m_text[m_offset] == '\t'
                    || m_text[m_offset] == '\n' || m_text[m_offset] == '\r'))
            {
                ++m_offset;
            }
        }

        std::string_view m_text;
        std::size_t m_offset{};
    };

    // Reads one object, handing each key to `field`, which reads its value and
    // returns false when the value is unusable. A key seen twice fails.
    template <typename Field>
    [[nodiscard]] bool ReadObject(Reader& reader, Field&& field)
    {
        if (!reader.Consume('{'))
        {
            return false;
        }
        if (reader.Consume('}'))
        {
            return true;
        }
        std::vector<std::wstring> seen;
        do
        {
            auto key = reader.String();
            if (!key || !reader.Consume(':'))
            {
                return false;
            }
            if (std::find(seen.begin(), seen.end(), *key) != seen.end())
            {
                return false;
            }
            if (!field(*key))
            {
                return false;
            }
            seen.push_back(std::move(*key));
        } while (reader.Consume(','));
        return reader.Consume('}');
    }

    [[nodiscard]] std::optional<Segments::SavedSegment> ReadSavedSegment(Reader& reader)
    {
        std::optional<std::uint64_t> start;
        std::optional<std::uint64_t> end;
        std::optional<std::uint64_t> done;
        auto const read = ReadObject(reader, [&reader, &start, &end, &done](std::wstring const& key)
        {
            if (key == L"start") return (start = reader.Unsigned()).has_value();
            if (key == L"end") return (end = reader.Unsigned()).has_value();
            if (key == L"done") return (done = reader.Unsigned()).has_value();
            return reader.SkipValue(2);
        });
        if (!read || !start || !end || !done)
        {
            return std::nullopt;
        }
        return Segments::SavedSegment{ .Start = *start, .End = *end, .Done = *done };
    }

    [[nodiscard]] bool ReadSavedSegments(Reader& reader, std::vector<Segments::SavedSegment>& segments)
    {
        if (!reader.Consume('['))
        {
            return false;
        }
        if (reader.Consume(']'))
        {
            return true;
        }
        do
        {
            // Bounded before parsing on, so an oversized table costs little.
            if (segments.size() > Segments::MaxSavedSegments)
            {
                return false;
            }
            auto segment = ReadSavedSegment(reader);
            if (!segment)
            {
                return false;
            }
            segments.push_back(*segment);
        } while (reader.Consume(','));
        return reader.Consume(']');
    }
}

namespace HaloDesktop::Services::Downloads::Segments
{
    std::uint64_t PlanSegment::Remaining() const noexcept
    {
        return End > Position ? End - Position : 0;
    }

    std::optional<PlanStep> ChooseWork(std::span<PlanSegment const> segments, std::uint64_t minSplitBytes) noexcept
    {
        std::optional<std::size_t> bestIdle;
        std::optional<std::size_t> bestBusy;
        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            auto const& segment = segments[index];
            if (segment.Remaining() == 0)
            {
                continue;
            }
            auto& best = segment.Assigned ? bestBusy : bestIdle;
            if (!best || segment.Remaining() > segments[*best].Remaining())
            {
                best = index;
            }
        }
        if (bestIdle)
        {
            return PlanStep{ .Kind = PlanStepKind::Resume, .Index = *bestIdle };
        }
        if (!bestBusy)
        {
            return std::nullopt;
        }
        auto const& busy = segments[*bestBusy];
        auto const remaining = busy.Remaining();
        if (remaining < minSplitBytes)
        {
            return std::nullopt;
        }
        return PlanStep{ .Kind = PlanStepKind::Split, .Index = *bestBusy, .At = busy.Position + remaining / 2 };
    }

    std::chrono::milliseconds RetryDelay(std::uint32_t attempt, std::optional<std::uint64_t> retryAfterSeconds) noexcept
    {
        if (retryAfterSeconds)
        {
            auto const capped = (std::min)(*retryAfterSeconds, static_cast<std::uint64_t>(MaxRetryAfter.count()));
            return std::chrono::seconds{ static_cast<std::chrono::seconds::rep>(capped) };
        }
        auto const step = (std::min)(attempt > 0 ? attempt - 1 : 0u, 2u);
        return std::chrono::seconds{ 1ll << step };
    }

    ConnectionBudget::ConnectionBudget(std::size_t max, Clock::duration interval, Clock::time_point now) noexcept
        : m_max((std::max)(max, std::size_t{ 1 })),
          m_interval(interval),
          m_ceiling(m_max),
          m_nextRamp(now + interval)
    {
    }

    std::size_t ConnectionBudget::Cap() const noexcept
    {
        return (std::min)(m_max, m_ceiling);
    }

    void ConnectionBudget::Ramp(Clock::time_point now) noexcept
    {
        if (m_allowed < Cap() && now >= m_nextRamp)
        {
            ++m_allowed;
            m_nextRamp = now + m_interval;
        }
    }

    std::size_t ConnectionBudget::Allowed() const noexcept
    {
        return m_allowed;
    }

    void ConnectionBudget::Refuse(std::size_t live) noexcept
    {
        m_ceiling = (std::min)(m_ceiling, (std::max)(live, std::size_t{ 1 }));
        m_allowed = (std::min)(m_allowed, m_ceiling);
    }

    std::optional<Clock::time_point> ConnectionBudget::NextRamp() const noexcept
    {
        if (m_allowed < Cap())
        {
            return m_nextRamp;
        }
        return std::nullopt;
    }

    bool Segment::IsComplete() const noexcept
    {
        return End != Unbounded && Position >= End;
    }

    OverlapCheck Segment::VerifyOverlap()
    {
        if (m_overlapFromPrevious.empty() || !m_headFromStart)
        {
            return OverlapCheck::Consistent;
        }
        auto const compared = (std::min)(m_head.size(), m_overlapFromPrevious.size());
        if (!std::equal(m_head.begin(), m_head.begin() + static_cast<std::ptrdiff_t>(compared), m_overlapFromPrevious.begin()))
        {
            return OverlapCheck::Mismatch;
        }
        if (compared == m_overlapFromPrevious.size())
        {
            m_overlapFromPrevious.clear();
        }
        return OverlapCheck::Consistent;
    }

    std::string Encode(SavedTable const& table)
    {
        std::string output;
        output.reserve(64 + table.Segments.size() * 48);
        output += "{\"version\":";
        AppendUnsigned(output, table.Version);
        output += ",\"total\":";
        AppendUnsigned(output, table.Total);
        output += ",\"validator\":";
        AppendString(output, table.Validator);
        output += ",\"segments\":[";
        for (std::size_t index = 0; index < table.Segments.size(); ++index)
        {
            auto const& segment = table.Segments[index];
            if (index > 0) output.push_back(',');
            output += "{\"start\":";
            AppendUnsigned(output, segment.Start);
            output += ",\"end\":";
            AppendUnsigned(output, segment.End);
            output += ",\"done\":";
            AppendUnsigned(output, segment.Done);
            output.push_back('}');
        }
        output += "]}";
        return output;
    }

    std::optional<SavedTable> Decode(std::string_view raw)
    {
        try
        {
            Reader reader{ raw };
            std::optional<std::uint64_t> version;
            std::optional<std::uint64_t> total;
            std::optional<std::wstring> validator;
            std::optional<std::vector<SavedSegment>> segments;
            auto const read = ReadObject(reader, [&reader, &version, &total, &validator, &segments](std::wstring const& key)
            {
                if (key == L"version") return (version = reader.Unsigned()).has_value();
                if (key == L"total") return (total = reader.Unsigned()).has_value();
                if (key == L"validator") return (validator = reader.String()).has_value();
                if (key == L"segments")
                {
                    segments.emplace();
                    return ReadSavedSegments(reader, *segments);
                }
                return reader.SkipValue(1);
            });
            if (!read || !reader.AtEnd() || !version || !total || !validator || !segments
                || *version > (std::numeric_limits<std::uint32_t>::max)())
            {
                return std::nullopt;
            }
            return SavedTable{
                .Version = static_cast<std::uint32_t>(*version),
                .Total = *total,
                .Validator = std::move(*validator),
                .Segments = std::move(*segments),
            };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::filesystem::path TablePath(std::filesystem::path const& part)
    {
        auto value = part;
        value += L".segments";
        return value;
    }

    std::filesystem::path TableTempPath(std::filesystem::path const& table)
    {
        auto value = table;
        value += L".tmp";
        return value;
    }

    SegmentTable SegmentTable::Contiguous(std::uint64_t done, std::uint64_t end)
    {
        SegmentTable table;
        if (done > 0)
        {
            table.Push(0, done, done);
        }
        if (end > done)
        {
            table.Push(done, end, done);
        }
        return table;
    }

    std::optional<SegmentTable> SegmentTable::Restore(
        SavedTable const& saved,
        std::uint64_t total,
        std::optional<std::wstring_view> validator,
        std::uint64_t partLength)
    {
        if (!validator || validator->empty())
        {
            return std::nullopt;
        }
        if (saved.Version != TableVersion
            || total == 0
            || saved.Total != total
            || saved.Validator != *validator
            || partLength > total
            || saved.Segments.empty()
            || saved.Segments.size() > MaxSavedSegments)
        {
            return std::nullopt;
        }
        std::vector<SavedSegment> folded;
        folded.reserve(saved.Segments.size());
        std::uint64_t expected{};
        for (auto const& segment : saved.Segments)
        {
            if (segment.End < segment.Start)
            {
                return std::nullopt;
            }
            auto const length = segment.End - segment.Start;
            if (segment.Start != expected
                || length == 0
                || segment.Done > length
                || segment.Start + segment.Done > partLength)
            {
                return std::nullopt;
            }
            expected = segment.End;
            if (!folded.empty() && folded.back().Done == folded.back().End - folded.back().Start)
            {
                folded.back().Done += segment.Done;
                folded.back().End = segment.End;
            }
            else
            {
                folded.push_back(segment);
            }
        }
        if (expected != total)
        {
            return std::nullopt;
        }
        SegmentTable table;
        table.m_segments.reserve(folded.size());
        for (auto const& segment : folded)
        {
            table.Push(segment.Start, segment.End, segment.Start + segment.Done);
        }
        return table;
    }

    SavedTable SegmentTable::Saved(std::uint64_t total, std::wstring const& validator) const
    {
        SavedTable result{ .Version = TableVersion, .Total = total, .Validator = validator };
        result.Segments.reserve(m_segments.size());
        for (auto const& segment : m_segments)
        {
            result.Segments.push_back(SavedSegment{
                .Start = segment.Start,
                .End = segment.End,
                .Done = (std::min)(segment.Position, segment.End) - segment.Start,
            });
        }
        return result;
    }

    void SegmentTable::Push(std::uint64_t start, std::uint64_t end, std::uint64_t position)
    {
        Segment segment;
        segment.Key = m_nextKey++;
        segment.Start = start;
        segment.End = end;
        segment.Position = position;
        m_segments.push_back(std::move(segment));
    }

    std::vector<Segment> const& SegmentTable::Segments() const noexcept
    {
        return m_segments;
    }

    std::optional<std::size_t> SegmentTable::IndexOf(std::uint32_t key) const noexcept
    {
        for (std::size_t index = 0; index < m_segments.size(); ++index)
        {
            if (m_segments[index].Key == key) return index;
        }
        return std::nullopt;
    }

    Segment const* SegmentTable::Get(std::uint32_t key) const noexcept
    {
        auto const index = IndexOf(key);
        return index ? &m_segments[*index] : nullptr;
    }

    Segment* SegmentTable::GetMutable(std::uint32_t key) noexcept
    {
        auto const index = IndexOf(key);
        return index ? &m_segments[*index] : nullptr;
    }

    Segment const* SegmentTable::NextOf(std::uint32_t key) const noexcept
    {
        auto const* segment = Get(key);
        if (!segment)
        {
            return nullptr;
        }
        auto const end = segment->End;
        for (auto const& candidate : m_segments)
        {
            if (candidate.Start == end) return &candidate;
        }
        return nullptr;
    }

    Segment const* SegmentTable::PreviousOf(std::uint32_t key) const noexcept
    {
        auto const* segment = Get(key);
        if (!segment)
        {
            return nullptr;
        }
        auto const start = segment->Start;
        for (auto const& candidate : m_segments)
        {
            if (candidate.End == start && candidate.End != Unbounded) return &candidate;
        }
        return nullptr;
    }

    std::optional<PlanStep> SegmentTable::Choose(Clock::time_point now, std::uint64_t minSplitBytes) const
    {
        if (std::any_of(m_segments.begin(), m_segments.end(), [](Segment const& segment) { return segment.End == Unbounded; }))
        {
            return std::nullopt;
        }
        std::vector<PlanSegment> plan;
        plan.reserve(m_segments.size());
        for (auto const& segment : m_segments)
        {
            plan.push_back(PlanSegment{
                .Start = segment.Start,
                .End = segment.End,
                .Position = segment.Position,
                .Assigned = segment.Connection.has_value() || (segment.RetryAt && *segment.RetryAt > now),
            });
        }
        return ChooseWork(plan, minSplitBytes);
    }

    std::optional<std::uint32_t> SegmentTable::TakeStep(PlanStep step)
    {
        if (step.Kind == PlanStepKind::Split)
        {
            return Split(step.Index, step.At);
        }
        if (step.Index >= m_segments.size())
        {
            return std::nullopt;
        }
        return m_segments[step.Index].Key;
    }

    std::optional<std::uint32_t> SegmentTable::Split(std::size_t index, std::uint64_t at)
    {
        if (index >= m_segments.size())
        {
            return std::nullopt;
        }
        auto& segment = m_segments[index];
        if (at <= segment.Position || at >= segment.End)
        {
            return std::nullopt;
        }
        auto const end = segment.End;
        segment.End = at;
        Segment rest;
        rest.Key = m_nextKey++;
        rest.Start = at;
        rest.End = end;
        rest.Position = at;
        auto const key = rest.Key;
        m_segments.insert(m_segments.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(rest));
        return key;
    }

    bool SegmentTable::AbsorbNext(std::uint32_t key)
    {
        auto const index = IndexOf(key);
        if (!index)
        {
            return false;
        }
        auto const end = m_segments[*index].End;
        if (end == Unbounded)
        {
            return false;
        }
        auto const next = std::find_if(m_segments.begin(), m_segments.end(), [end](Segment const& segment)
        {
            return segment.Start == end;
        });
        if (next == m_segments.end() || next->Connection || next->Position != next->Start)
        {
            return false;
        }
        auto const newEnd = next->End;
        m_segments.erase(next);
        // The erased segment comes after `index`, so the index is still valid.
        m_segments[*index].End = newEnd;
        return true;
    }

    void SegmentTable::Begin(std::uint32_t key, std::uint64_t connection)
    {
        auto* segment = GetMutable(key);
        if (!segment)
        {
            return;
        }
        segment->Connection = connection;
        segment->RetryAt.reset();
        if (segment->Position == segment->Start)
        {
            segment->m_head.clear();
            segment->m_headFromStart = true;
        }
    }

    void SegmentTable::Release(std::uint32_t key) noexcept
    {
        if (auto* segment = GetMutable(key))
        {
            segment->Connection.reset();
        }
    }

    OverlapCheck SegmentTable::Record(std::uint32_t key, std::span<std::uint8_t const> bytes)
    {
        auto* segment = GetMutable(key);
        if (!segment)
        {
            return OverlapCheck::Consistent;
        }
        auto const into = segment->Position - segment->Start;
        if (segment->m_headFromStart
            && into < OverlapBytes
            && into == segment->m_head.size())
        {
            auto const take = (std::min)(OverlapBytes - segment->m_head.size(), bytes.size());
            segment->m_head.insert(segment->m_head.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
            if (segment->VerifyOverlap() == OverlapCheck::Mismatch)
            {
                return OverlapCheck::Mismatch;
            }
        }
        segment->Position += bytes.size();
        return OverlapCheck::Consistent;
    }

    std::size_t SegmentTable::OverlapWanted(std::uint32_t key, std::uint64_t total) const noexcept
    {
        auto const* segment = Get(key);
        auto const* next = NextOf(key);
        if (!segment || !next)
        {
            return 0;
        }
        if (!next->m_headFromStart && next->Position != next->Start)
        {
            return 0;
        }
        auto const left = total > segment->End ? total - segment->End : 0;
        return static_cast<std::size_t>((std::min)(left, static_cast<std::uint64_t>(OverlapBytes)));
    }

    OverlapCheck SegmentTable::HandOverOverlap(std::uint32_t key, std::vector<std::uint8_t> overlap)
    {
        auto const* segment = Get(key);
        if (!segment)
        {
            return OverlapCheck::Consistent;
        }
        auto const end = segment->End;
        auto const next = std::find_if(m_segments.begin(), m_segments.end(), [end](Segment const& candidate)
        {
            return candidate.Start == end;
        });
        if (next == m_segments.end())
        {
            return OverlapCheck::Consistent;
        }
        next->m_overlapFromPrevious = std::move(overlap);
        return next->VerifyOverlap();
    }

    std::uint64_t SegmentTable::Received() const noexcept
    {
        std::uint64_t total{};
        for (auto const& segment : m_segments)
        {
            total += (std::min)(segment.Position, segment.End) - segment.Start;
        }
        return total;
    }

    bool SegmentTable::IsComplete() const noexcept
    {
        return std::all_of(m_segments.begin(), m_segments.end(), [](Segment const& segment) { return segment.IsComplete(); });
    }

    std::optional<Clock::time_point> SegmentTable::NextRetry(Clock::time_point now) const noexcept
    {
        std::optional<Clock::time_point> earliest;
        for (auto const& segment : m_segments)
        {
            if (segment.Connection || !segment.RetryAt || *segment.RetryAt <= now)
            {
                continue;
            }
            if (!earliest || *segment.RetryAt < *earliest)
            {
                earliest = segment.RetryAt;
            }
        }
        return earliest;
    }
}
