/*
 *    Copyright (C) 2026 by RoboLab at the University of Extremadura
 *
 *    Licensed under the Apache License, Version 2.0 (the "License").
 */

#ifndef DDS_STATS_MONITOR_H
#define DDS_STATS_MONITOR_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// DdsStatsMonitor — optional Fast DDS Statistics consumer for the mind view.
//
// GATED OUT BY DEFAULT. The whole class only compiles when the CMake option RC_DDS_STATS is ON (which
// also needs RC_DDS_STATS_SRC pointing at a Fast-DDS source tree, since eProsima ships the statistics
// *type* headers only as .idl, not installed). A normal/deployment build defines nothing here, so this
// header is an empty shell and the mind view falls back to the loopback sniffer. Even in a stats build
// the monitor only starts when the env var RC_DDS_STATS=1 is set at runtime — a second, no-rebuild gate.
//
// When active it creates a read-only DomainParticipant on each requested domain (e.g. 0 = DSR/CRDT
// graph, 7 = media plane), subscribes to the reserved PUBLICATION_THROUGHPUT / SUBSCRIPTION_THROUGHPUT
// statistics topics, and aggregates bytes/s per participant (matched to its discovered name). Unlike the
// packet sniffer this is measured AT THE WRITER, so it sees zero-copy SHM media traffic too. The
// consumer participant does NOT enable statistics on itself, so it adds no feedback traffic.

// Per-participant throughput split by direction. PUBLICATION_THROUGHPUT is measured at the writer
// (bytes this participant SENDS = out); SUBSCRIPTION_THROUGHPUT at the reader (bytes it RECEIVES = in).
// Both are in bytes/s. Defined outside the RC_DDS_STATS gate so the widget compiles either way.
struct DdsInOut { double in = 0.0; double out = 0.0; };

#ifdef RC_DDS_STATS

#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/statistics/topic_names.hpp>

#include <statistics/types/typesPubSubTypes.hpp>   // from RC_DDS_STATS_SRC (Fast-DDS source tree)

namespace rc_ddsstats
{
    using Prefix = std::array<std::uint8_t, 12>;   // DDS GUID prefix = one participant

    // One watched domain: its read-only participant + the per-participant accumulators. DomainCtx is
    // defined BEFORE the listeners (which dereference it) and owns them by base-class pointer, so there
    // is no incomplete-type / ordering tangle.
    struct DomainCtx
    {
        std::uint32_t domain = 0;
        eprosima::fastdds::dds::DomainParticipant* participant = nullptr;
        std::mutex mtx;
        std::map<Prefix, double> out_bps;     // guid prefix -> bytes/s SENT  (publication throughput)
        std::map<Prefix, double> in_bps;      // guid prefix -> bytes/s RECV  (subscription throughput)
        std::map<Prefix, std::string> names;  // guid prefix -> discovered participant name
        std::vector<std::unique_ptr<eprosima::fastdds::dds::DataReaderListener>> rlisten;  // one per direction
        std::unique_ptr<eprosima::fastdds::dds::DomainParticipantListener> plisten;
    };

    inline std::string short_hex(const Prefix& p)
    {
        static const char* h = "0123456789abcdef";
        std::string s = "id:";
        for (int i = 0; i < 4; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 0xF]; }
        return s;
    }

    // Fold each throughput sample (bytes/s) into its participant's accumulator. One listener instance
    // per direction (outgoing == publication topic, !outgoing == subscription topic) so the sample lands
    // in the right map without having to inspect the topic name on every sample.
    struct ReaderListener : eprosima::fastdds::dds::DataReaderListener
    {
        ReaderListener(DomainCtx* c, bool out) : ctx(c), outgoing(out) {}
        void on_data_available(eprosima::fastdds::dds::DataReader* reader) override
        {
            namespace dds = eprosima::fastdds::dds;
            eprosima::fastdds::statistics::EntityData sample;
            dds::SampleInfo info;
            while (reader->take_next_sample(&sample, &info) == dds::RETCODE_OK)
            {
                if (!info.valid_data) continue;
                Prefix pfx{};
                const auto& gp = sample.guid().guidPrefix().value();   // array<uint8_t,12>
                for (std::size_t i = 0; i < pfx.size() && i < gp.size(); ++i) pfx[i] = gp[i];
                std::lock_guard<std::mutex> lk(ctx->mtx);
                double& v = (outgoing ? ctx->out_bps[pfx] : ctx->in_bps[pfx]);   // EMA over ~1 Hz cadence
                v = 0.5 * static_cast<double>(sample.data()) + 0.5 * v;
            }
        }
        DomainCtx* ctx;
        bool outgoing;
    };

    // Learn guid-prefix -> human participant name from discovery.
    struct PartListener : eprosima::fastdds::dds::DomainParticipantListener
    {
        explicit PartListener(DomainCtx* c) : ctx(c) {}
        void on_participant_discovery(eprosima::fastdds::dds::DomainParticipant*,
                                      eprosima::fastdds::rtps::ParticipantDiscoveryStatus,
                                      const eprosima::fastdds::dds::ParticipantBuiltinTopicData& info,
                                      bool&) override
        {
            Prefix pfx{};
            const auto& gp = info.guid.guidPrefix.value;
            for (std::size_t i = 0; i < pfx.size(); ++i) pfx[i] = gp[i];
            std::string name(info.participant_name.c_str());
            if (name.empty()) return;
            std::lock_guard<std::mutex> lk(ctx->mtx);
            ctx->names[pfx] = name;
        }
        DomainCtx* ctx;
    };
}  // namespace rc_ddsstats

class DdsStatsMonitor
{
    public:
        // domains: the DDS domain ids to watch (e.g. {0, 7}). Only starts if env RC_DDS_STATS=1.
        explicit DdsStatsMonitor(std::vector<std::uint32_t> domains)
        {
            const char* on = std::getenv("RC_DDS_STATS");
            enabled_ = (on != nullptr && std::string(on) == "1");
            if (!enabled_) return;
            for (auto d : domains) start_domain(d);
        }

        ~DdsStatsMonitor()
        {
            namespace dds = eprosima::fastdds::dds;
            for (auto& c : ctx_)
                if (c->participant)
                {
                    c->participant->delete_contained_entities();
                    dds::DomainParticipantFactory::get_instance()->delete_participant(c->participant);
                }
        }

        [[nodiscard]] bool enabled() const { return enabled_; }

        // Per-participant throughput on one domain: {display name -> {in, out} bytes/s}. Thread-safe snapshot.
        std::map<std::string, DdsInOut> per_participant(std::uint32_t domain)
        {
            std::map<std::string, DdsInOut> out;
            for (auto& c : ctx_)
                if (c->domain == domain)
                {
                    std::lock_guard<std::mutex> lk(c->mtx);
                    auto name_of = [&](const rc_ddsstats::Prefix& pfx) {
                        auto it = c->names.find(pfx);
                        return it != c->names.end() ? it->second : rc_ddsstats::short_hex(pfx);
                    };
                    for (const auto& [pfx, bps] : c->out_bps) out[name_of(pfx)].out += bps;
                    for (const auto& [pfx, bps] : c->in_bps)  out[name_of(pfx)].in  += bps;
                }
            return out;
        }

    private:
        void start_domain(std::uint32_t domain)
        {
            namespace dds = eprosima::fastdds::dds;
            auto ctx = std::make_unique<rc_ddsstats::DomainCtx>();
            ctx->domain = domain;
            ctx->plisten = std::make_unique<rc_ddsstats::PartListener>(ctx.get());

            auto* factory = dds::DomainParticipantFactory::get_instance();
            ctx->participant = factory->create_participant(
                domain, dds::PARTICIPANT_QOS_DEFAULT, ctx->plisten.get(), dds::StatusMask::none());
            if (ctx->participant == nullptr)
                return;   // domain not up / creation failed → skip silently (best-effort monitor)

            dds::TypeSupport type(new eprosima::fastdds::statistics::EntityDataPubSubType());
            type.register_type(ctx->participant);
            auto* sub = ctx->participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);

            // Publication throughput → outgoing; subscription throughput → incoming. Each topic gets its
            // OWN listener carrying the direction, so on_data_available never has to look up the topic name.
            const struct { const char* topic; bool outgoing; } feeds[] = {
                { eprosima::fastdds::statistics::PUBLICATION_THROUGHPUT_TOPIC,  true  },
                { eprosima::fastdds::statistics::SUBSCRIPTION_THROUGHPUT_TOPIC, false },
            };
            for (const auto& f : feeds)
            {
                // create_topic returns null if the topic already exists (e.g. this participant also has
                // statistics enabled) — fall back to the existing one so the reader is still created.
                dds::TopicDescription* topic =
                    ctx->participant->create_topic(f.topic, type.get_type_name(), dds::TOPIC_QOS_DEFAULT);
                if (topic == nullptr)
                    topic = ctx->participant->lookup_topicdescription(f.topic);
                if (topic != nullptr && sub != nullptr)
                {
                    auto lst = std::make_unique<rc_ddsstats::ReaderListener>(ctx.get(), f.outgoing);
                    sub->create_datareader(topic, dds::DATAREADER_QOS_DEFAULT, lst.get());
                    ctx->rlisten.push_back(std::move(lst));
                }
            }
            ctx_.push_back(std::move(ctx));
        }

        bool enabled_ = false;
        std::vector<std::unique_ptr<rc_ddsstats::DomainCtx>> ctx_;
};

#else   // RC_DDS_STATS not defined → empty shell so the widget compiles unchanged

class DdsStatsMonitor
{
    public:
        explicit DdsStatsMonitor(std::vector<std::uint32_t> /*domains*/) {}
        [[nodiscard]] bool enabled() const { return false; }
        std::map<std::string, DdsInOut> per_participant(std::uint32_t /*domain*/) { return {}; }
};

#endif  // RC_DDS_STATS
#endif  // DDS_STATS_MONITOR_H
