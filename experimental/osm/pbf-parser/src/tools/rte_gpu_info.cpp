#include "rtegpu_common.h"

#include <iostream>

static const char *section_name(uint32_t type) {
    switch (type) {
        case RTEGPU_SEC_STOP_LAT: return "stop_lat";
        case RTEGPU_SEC_STOP_LON: return "stop_lon";
        case RTEGPU_SEC_STOP_MODE: return "stop_mode";
        case RTEGPU_SEC_STOP_NAME_OFFSET: return "stop_name_offset";
        case RTEGPU_SEC_STOP_NAME_SIZE: return "stop_name_size";
        case RTEGPU_SEC_TRIP_ROUTE: return "trip_route";
        case RTEGPU_SEC_TRIP_SERVICE: return "trip_service";
        case RTEGPU_SEC_TRIP_MODE: return "trip_mode";
        case RTEGPU_SEC_TRIP_EVENT_OFFSET: return "trip_event_offset";
        case RTEGPU_SEC_TRIP_EVENT_COUNT: return "trip_event_count";
        case RTEGPU_SEC_EVENT_STOP: return "event_stop";
        case RTEGPU_SEC_EVENT_ARRIVAL: return "event_arrival";
        case RTEGPU_SEC_EVENT_DEPARTURE: return "event_departure";
        case RTEGPU_SEC_SERVICE_ACTIVE: return "service_active";
        case RTEGPU_SEC_ROUTE_SHORT_OFFSET: return "route_short_offset";
        case RTEGPU_SEC_ROUTE_SHORT_SIZE: return "route_short_size";
        case RTEGPU_SEC_ROUTE_LONG_OFFSET: return "route_long_offset";
        case RTEGPU_SEC_ROUTE_LONG_SIZE: return "route_long_size";
        case RTEGPU_SEC_ROUTE_MODE: return "route_mode";
        case RTEGPU_SEC_ROUTE_TYPE: return "route_type";
        case RTEGPU_SEC_TRANSFER_OFFSET: return "transfer_offset";
        case RTEGPU_SEC_TRANSFER_TO: return "transfer_to";
        case RTEGPU_SEC_TRANSFER_WALK_SEC: return "transfer_walk_sec";
        case RTEGPU_SEC_STRING_TABLE: return "string_table";
        default: return "unknown";
    }
}

int main(int argc, char **argv) {
    bool show_sections = false;
    const char *path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sections") == 0) show_sections = true;
        else path = argv[i];
    }
    if (!path) {
        std::fprintf(stderr, "Usage: %s FILE.rtegpu [--sections]\n", argv[0]);
        return 1;
    }
    try {
        RteGpuPack pack = rtegpu_load_pack(path, true);
        std::printf("format: RTEGPU01\n");
        std::printf("file_size: %llu\n", (unsigned long long)pack.header.file_size);
        std::printf("stops: %llu\n", (unsigned long long)pack.header.stop_count);
        std::printf("routes: %llu\n", (unsigned long long)pack.header.route_count);
        std::printf("services: %llu\n", (unsigned long long)pack.header.service_count);
        std::printf("trips: %llu\n", (unsigned long long)pack.header.trip_count);
        std::printf("events: %llu\n", (unsigned long long)pack.header.event_count);
        std::printf("transfer_edges: %llu\n", (unsigned long long)pack.header.transfer_edge_count);
        std::printf("first_service_date: %u\n", pack.header.first_service_date);
        std::printf("service_date_count: %u\n", pack.header.service_date_count);
        std::printf("section_count: %u\n", pack.header.section_count);
        std::printf("hot_arrays_loaded: yes\n");
        if (show_sections) {
            for (const RteGpuSection &section : pack.sections) {
                std::printf("section: type=%u name=%s offset=%llu size=%llu count=%llu record_size=%u\n",
                    section.type,
                    section_name(section.type),
                    (unsigned long long)section.offset,
                    (unsigned long long)section.size,
                    (unsigned long long)section.count,
                    section.record_size);
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rte-gpu-info: %s\n", e.what());
        return 1;
    }
}
