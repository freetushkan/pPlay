cmake_minimum_required(VERSION 3.10)
#set(CMAKE_VERBOSE_MAKEFILE ON)

####################
# TARGET PLATFORM
####################
set(PLATFORM_LINUX OFF CACHE BOOL "Linux platform target")
set(PLATFORM_WINDOWS OFF CACHE BOOL "Windows platform target (msys/mingw64)")
set(PLATFORM_RPI3 OFF CACHE BOOL "RIP3 platform target")
set(PLATFORM_SWITCH OFF CACHE BOOL "Nintendo Switch platform target")
set(PLATFORM_3DS OFF CACHE BOOL "Nintendo 3DS platform target")
set(PLATFORM_VITA OFF CACHE BOOL "Sony PS Vita platform target")

####################
# BUILD OPTIONS
####################
set(OPTION_SMB2 OFF CACHE BOOL "Build with smb2 support")
set(OPTION_SCRAPPING OFF CACHE BOOL "Build with TMDB scraping support")

###################
# MODULES
###################
if (PLATFORM_PS4)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:cstdlib>)
endif ()

add_subdirectory(libcross2d)

##############
# pplay
##############
project(pplay)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# pplay version
set(VERSION_MAJOR "4")
set(VERSION_MINOR "4")
set(PROJECT_AUTHOR "Cpasjuste")
set(TITLE_ID "PPLAY0001")
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")

# deps
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# sources
file(GLOB PPLAY_SRC
        src/*.c*
        src/filer/*.c*
        src/filer/Browser/*.c*
        src/filer/ftplib/*.c*
        src/menus/*.c*
        src/player/*.c*
        )

# includes
set(PPLAY_INC
        src
        src/filer
        src/filer/Browser
        src/filer/ftplib
        src/menus
        src/player
        ${MPV_INCLUDE_DIRS}
        ${ASS_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        ${BZIP2_INCLUDE_DIR}
        ${CURL_INCLUDE_DIRS}
        )

# cflags
set(PPLAY_CFLAGS
        -D__PPLAY__
        -D__VERSION_MAJOR__=${VERSION_MAJOR}
        -D__VERSION_MINOR__=${VERSION_MINOR}
        #-DM_PI=3.14159265358979323846
        #-DM_SQRT2=1.41421356237309504880
        )

# ldflags
set(PPLAY_LDFLAGS
        ${MPV_LIBRARY}
        ${ASS_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${BZIP2_LIBRARIES}
        ${CURL_LIBRARIES}
        Threads::Threads
        )


if (NOT OPTION_SCRAPPING)
    list(REMOVE_ITEM PPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/filer/scrap_view.cpp)
endif ()

if (OPTION_SCRAPPING)
    add_subdirectory(pscrap)
    file(GLOB PPLAY_SCRAPPER_SRC src/scrapper/*.c*)
    list(APPEND PPLAY_SRC ${PPLAY_SCRAPPER_SRC})
    list(APPEND PPLAY_INC src/scrapper)
    list(APPEND PPLAY_CFLAGS -DPPLAY_ENABLE_SCRAPPING)
    list(APPEND PPLAY_LDFLAGS pscrap)
endif ()

if (OPTION_SMB2)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(OPENSSL_INCLUDE_DIR "" CACHE STRING "" FORCE)
    add_subdirectory(libsmb2)
    list(APPEND PPLAY_CFLAGS -D__SMB2__)
    list(APPEND PPLAY_INC libsmb2/include libsmb2/include/smb2)
    list(APPEND PPLAY_LDFLAGS smb2)
endif ()

#####################
# PLATORM SPECIFIC
#####################
if (PLATFORM_SWITCH)
    #####################
    # SWITCH PLATORM
    #####################
    set(MPV_LIBRARIES mpv)
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS
            swscale swresample avformat avfilter avcodec avutil
            lzma opus vpx ass freetype fribidi png bz2 usbhsfs ntfs-3g lwext4
            )
elseif (PLATFORM_PS4)
    set(PS4_PKG_TITLE "pPlay @ cpasjuste")
    set(PS4_PKG_TITLE_ID "PPLA00001")
    set(PS4_PKG_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}")
    list(APPEND PPLAY_INC)
    list(APPEND PPLAY_LDFLAGS opus freetype fribidi png bz2)
elseif (PLATFORM_LINUX)
    #####################
    # LINUX PLATORM
    #####################
endif ()

#####################
# openscreen (Cast support)
#####################
set(OPENSCREEN_ROOT "${CMAKE_SOURCE_DIR}/openscreen" CACHE STRING "OpenScreen root directory")
set(OPENSCREEN_GEN "${OPENSCREEN_ROOT}/out/Default/gen")
set(OPENSCREEN_PROTOBUF_SRC "${OPENSCREEN_ROOT}/third_party/protobuf/src")
set(OPENSCREEN_ABSEIL_SRC "${OPENSCREEN_ROOT}/third_party/abseil/src")
set(OPENSCREEN_JSON_SRC "${OPENSCREEN_ROOT}/third_party/jsoncpp/src/include")

set(OPENSCREEN_BORINGSSL_INCLUDE "${OPENSCREEN_ROOT}/third_party/boringssl/include")
set(OPENSCREEN_POLYFILLS_INCLUDE "${OPENSCREEN_ROOT}/CXX20_polyfills")

if(EXISTS "${OPENSCREEN_ROOT}" AND EXISTS "${OPENSCREEN_GEN}")
    add_compile_definitions(__PS4__)
    add_compile_definitions(OS_FREEBSD)
    add_compile_definitions(__x86_64__)

    include_directories(BEFORE 
        ${OPENSCREEN_ROOT}
        ${OPENSCREEN_ROOT}/openscreen
        ${OPENSCREEN_GEN}
        ${OPENSCREEN_GEN}/cast/common/channel/proto
        ${OPENSCREEN_PROTOBUF_SRC}
        ${OPENSCREEN_ABSEIL_SRC}
        ${OPENSCREEN_JSON_SRC}
        ${OPENSCREEN_BORINGSSL_INCLUDE}
        ${OPENSCREEN_POLYFILLS_INCLUDE}
        ${OPENSCREEN_ROOT}/third_party/protobuf/third_party/utf8_range
        ${CMAKE_SOURCE_DIR}/ps4-openssl/boringssl/include
    )

    list(APPEND PPLAY_SRC
        ${OPENSCREEN_ROOT}/platform/api/udp_socket.cc
        ${OPENSCREEN_ROOT}/platform/base/error.cc
        ${OPENSCREEN_ROOT}/platform/base/location.cc
        ${OPENSCREEN_ROOT}/platform/base/interface_info.cc
        ${OPENSCREEN_ROOT}/platform/base/ip_address.cc
        ${OPENSCREEN_ROOT}/platform/base/tls_credentials.cc
        ${OPENSCREEN_ROOT}/platform/base/udp_packet.cc
        ${OPENSCREEN_ROOT}/platform/impl/task_runner.cc
        ${OPENSCREEN_ROOT}/platform/impl/platform_client_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/socket_address_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/socket_handle_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/socket_handle_waiter.cc
        ${OPENSCREEN_ROOT}/platform/impl/socket_handle_waiter_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/tls_data_router_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/tls_write_buffer.cc
        ${OPENSCREEN_ROOT}/platform/impl/logging_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/time.cc
        ${OPENSCREEN_ROOT}/platform/impl/network_interface.cc
        ${OPENSCREEN_ROOT}/platform/impl/timeval_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/udp_socket_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/udp_socket_reader_posix.cc
        ${OPENSCREEN_ROOT}/cast/receiver/application_agent.cc
        ${OPENSCREEN_ROOT}/cast/receiver/channel/static_credentials.cc
        ${OPENSCREEN_ROOT}/cast/receiver/channel/device_auth_namespace_handler.cc
        ${OPENSCREEN_ROOT}/cast/receiver/channel/receiver_socket_factory.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/cast_service.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/mirroring_application.cc
        ${OPENSCREEN_ROOT}/cast/common/channel/virtual_connection_router.cc
        ${OPENSCREEN_ROOT}/cast/common/channel/connection_namespace_handler.cc
        ${OPENSCREEN_ROOT}/cast/common/channel/cast_socket_message_port.cc
        ${OPENSCREEN_ROOT}/cast/common/public/receiver_info.cc
        ${OPENSCREEN_ROOT}/util/json/json_serialization.cc
        ${OPENSCREEN_ROOT}/util/json/json_value.cc
        ${OPENSCREEN_ROOT}/util/crypto/certificate_utils.cc
        ${OPENSCREEN_ROOT}/util/crypto/openssl_util.cc
        ${OPENSCREEN_ROOT}/util/crypto/digest_sign.cc
        ${OPENSCREEN_ROOT}/util/crypto/random_bytes.cc
        ${OPENSCREEN_ROOT}/util/string_util.cc
        ${OPENSCREEN_ROOT}/cast/common/channel/message_util.cc
        ${OPENSCREEN_GEN}/cast/common/channel/proto/cast_channel.pb.cc
        ${OPENSCREEN_GEN}/cast/common/channel/proto/authority_keys.pb.cc
        ${OPENSCREEN_ROOT}/third_party/jsoncpp/src/src/lib_json/json_reader.cpp
        ${OPENSCREEN_ROOT}/third_party/jsoncpp/src/src/lib_json/json_value.cpp
        ${OPENSCREEN_ROOT}/third_party/jsoncpp/src/src/lib_json/json_writer.cpp
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/stubs/common.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/coded_stream.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/zero_copy_stream.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/wire_format_lite.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/wire_format.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/message_lite.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/message.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/repeated_field.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/extension_set.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_util.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_tctable_lite.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_tctable_full.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_tctable_gen.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_bases.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_message_reflection.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/generated_enum_util.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/repeated_ptr_field.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/reflection_ops.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/descriptor.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/descriptor.pb.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/extension_set_heavy.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/any.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/arena.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/arenastring.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/port.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/parse_context.cc

        ${OPENSCREEN_ROOT}/third_party/protobuf/third_party/utf8_range/utf8_range.c

        ${OPENSCREEN_ROOT}/platform/impl/tls_connection_factory_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/tls_connection_posix.cc
        ${OPENSCREEN_ROOT}/platform/impl/stream_socket_posix.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/service_dispatcher.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/public/dns_sd_instance.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_service_impl.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/public/dns_sd_instance_endpoint.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/public/dns_sd_txt_record.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/receiver_session.cc
        ${OPENSCREEN_ROOT}/cast/common/public/cast_streaming_app_ids.cc
        ${OPENSCREEN_ROOT}/cast/common/channel/cast_socket.cc

        ${OPENSCREEN_ROOT}/platform/api/task_runner_deleter.cc
        ${OPENSCREEN_ROOT}/platform/api/tls_connection_factory.cc
        ${OPENSCREEN_ROOT}/platform/api/tls_connection.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/streaming_playback_controller.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/environment.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/receiver_constraints.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/session_messenger.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/receiver_message.cc
        ${OPENSCREEN_ROOT}/cast/streaming/sender_message.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/publisher_impl.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/querier_impl.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/service_instance.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/conversion_layer.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_responder.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_querier.cc

        ${OPENSCREEN_ROOT}/util/stringprintf.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/simple_remoting_receiver.cc
        ${OPENSCREEN_ROOT}/cast/standalone_receiver/dummy_player.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/log_message.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/log_sink_set.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/nullguard.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/globals.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/check_op.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/globals.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/cord.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cord_internal.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cord_rep_btree.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cord_rep_btree_reader.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cord_rep_btree_navigator.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cord_rep_consume.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cordz_info.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cordz_handle.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/cordz_functions.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/str_cat.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/ostringstream.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/map_field.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/map.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/unknown_field_set.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/mutex.h
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/mutex.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/low_level_alloc.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/thread_identity.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/sysinfo.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/spinlock.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/spinlock_wait.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/hash/internal/hash.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/container/internal/raw_hash_set.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/arg.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/bind.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/extension.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/float_conversion.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/output.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/str_format/parser.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/stringify_sink.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/charconv_bigint.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/charconv_parse.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/charconv.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/hash/internal/city.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/create_thread_identity.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/per_thread_sem.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/pthread_waiter.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/waiter_base.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/graphcycles.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/kernel_timeout.cc

        ${OPENSCREEN_ROOT}/util/alarm.cc
        ${OPENSCREEN_ROOT}/util/base64.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/session_config.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/offer_messages.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/answer_messages.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/capture_recommendations.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/encoded_frame.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/receiver.cc
        ${OPENSCREEN_ROOT}/cast/streaming/public/rpc_messenger.cc
        ${OPENSCREEN_ROOT}/cast/streaming/impl/receiver_impl.cc
        ${OPENSCREEN_ROOT}/cast/streaming/impl/receiver_packet_router.cc
        ${OPENSCREEN_ROOT}/cast/streaming/impl/statistics_collector.cc
        ${OPENSCREEN_ROOT}/cast/streaming/impl/ntp_time.cc

        ${OPENSCREEN_GEN}/cast/streaming/remoting.pb.cc

        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/dns_data_graph.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/instance_key.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/conversion_layer.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_probe_manager.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_probe.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_publisher.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/public/mdns_records.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/network_interface_config.cc
        ${OPENSCREEN_ROOT}/discovery/dnssd/impl/service_key.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_receiver.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_sender.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_trackers.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/impl/mdns_responder.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/public/mdns_reader.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/public/mdns_writer.cc
        ${OPENSCREEN_ROOT}/discovery/mdns/public/mdns_service.cc

        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/dynamic_message.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/text_format.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/descriptor_database.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/feature_resolver.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/micro_string.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/tokenizer.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/io/strtod.cc
        ${OPENSCREEN_ROOT}/third_party/protobuf/src/google/protobuf/cpp_features.pb.cc

        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/ascii.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/escaping.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/charconv.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/numbers.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/substitute.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/escaping.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/utf8.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/strings/internal/memutil.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/throw_delegate.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/raw_logging.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/base/internal/strerror.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/numeric/int128.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/hash/internal/city.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/proto.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/log/internal/log_format.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/status/internal/status_internal.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/crc/internal/crc_cord_state.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/crc/internal/crc.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/crc/internal/crc_memcpy_fallback.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/per_thread_sem.cc
        ${OPENSCREEN_ROOT}/third_party/abseil/src/absl/synchronization/internal/create_thread_identity.cc
    )

    list(APPEND PPLAY_INC 
        ${OPENSCREEN_ROOT}
        ${OPENSCREEN_ROOT}/openscreen
        ${OPENSCREEN_GEN}
        ${OPENSCREEN_GEN}/cast/common/channel/proto
        ${OPENSCREEN_PROTOBUF_SRC}
        ${OPENSCREEN_ABSEIL_SRC}
        ${OPENSCREEN_JSON_SRC}
        ${OPENSCREEN_BORINGSSL_INCLUDE}
        ${OPENSCREEN_POLYFILLS_INCLUDE}
    )

    # Link OpenScreen libraries
    list(APPEND PPLAY_LDFLAGS ssl crypto)

    message(STATUS "OpenScreen support enabled at ${OPENSCREEN_ROOT}")
else()
    message(WARNING "OpenScreen not found or not built at ${OPENSCREEN_ROOT}")
    message(STATUS "Cast support will be disabled - continuing build without it")
endif()

#####################
# pplay executable
#####################
add_executable(${CMAKE_PROJECT_NAME} ${PPLAY_SRC})
target_include_directories(${CMAKE_PROJECT_NAME} BEFORE PRIVATE ${PPLAY_INC})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${PPLAY_CFLAGS})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
set(PLATFORM_LIBS)
if (PLATFORM_SWITCH)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto mbedx509 nx)
elseif (PLATFORM_PS4)
    find_package(fmt REQUIRED)
    list(APPEND PLATFORM_LIBS mbedtls mbedcrypto mbedx509 SceNet fmt::fmt)
endif ()
target_link_libraries(${CMAKE_PROJECT_NAME} cross2d ${PPLAY_LDFLAGS} ${PLATFORM_LIBS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE 
    APP_VERSION="${APP_VERSION}"
)
if(NOT MSVC)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--allow-multiple-definition")
    if(CMAKE_LINKER MATCHES "lld")
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--error-limit=0")
    endif()
endif()

#####################
# targets
#####################
include(libcross2d/cmake/targets.cmake)
