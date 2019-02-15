#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/yasio.h"
#include "../../src/ibinarystream.h"
#include "../../src/obinarystream.h"

#if defined(_WIN32)
#  include <Shlwapi.h>
#  pragma comment(lib, "shlwapi.lib")
#  define strcasestr StrStrIA
#endif

using namespace purelib::inet;

template <size_t _Size> void append_string(std::vector<char> &packet, const char (&message)[_Size])
{
  packet.insert(packet.end(), message, message + _Size - 1);
}

void yasioTest()
{
  purelib::inet::io_hostent endpoints[] = {{"www.ip138.com", 80},       // http client
                                           {"192.168.103.183", 59281},  // udp server
                                           {"192.168.103.183", 59281}}; // udp client

  obinarystream obs;
  obs.push24();
  obs.write_i24(16777215);
  obs.write_i24(16777213);
  obs.write_i24(259);
  obs.write_i24(16777217); // uint24 value overflow test
  obs.pop24();

  ibinarystream ibs(obs.data(), obs.length());
  auto n  = ibs.read_i24();
  auto v1 = ibs.read_i24();
  auto v2 = ibs.read_i24();
  auto v3 = ibs.read_i24();
  auto v4 = ibs.read_i24();

  io_service service;

  service.set_option(YASIO_OPT_TCP_KEEPALIVE, 60, 30, 3);

  resolv_fn_t resolv = [&](std::vector<ip::endpoint> &endpoints, const char *hostname,
                           unsigned short port) {
    return service.resolve(endpoints, hostname, port);
  };
  service.set_option(YASIO_OPT_RESOLV_FUNCTION, &resolv);


  std::vector<std::shared_ptr<io_transport>> transports;
  service.set_option(YASIO_OPT_LFIB_PARAMS, 16384, -1, 0, 0);
  service.set_option(YASIO_OPT_LOG_FILE, "yasio.log");

  deadline_timer udpconn_delay(service);
  deadline_timer udp_heartbeat(service);
  service.start_service(endpoints, _ARRAYSIZE(endpoints), [&](event_ptr event) {
    switch (event->type())
    {
      case YASIO_EVENT_RECV_PACKET:
      {
        auto packet = event->take_packet();
        packet.push_back('\0');
        printf("index:%d, receive data:%s", event->transport()->channel_index(), packet.data());
        if (event->channel_index() == 1)
        { // response udp client
          std::vector<char> packet;
          append_string(packet, "hello udp client!\n");
          service.write(event->transport(), std::move(packet));
        }
        break;
      }
      case YASIO_EVENT_CONNECT_RESPONSE:
        if (event->status() == 0)
        {
          auto transport = event->transport();
          if (event->channel_index() == 0)
          {
            std::vector<char> packet;
            append_string(packet, "GET /index.htm HTTP/1.1\r\n");

            append_string(packet, "Host: www.ip138.com\r\n");

            append_string(packet, "User-Agent: Mozilla/5.0 (Windows NT 10.0; "
                                  "WOW64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                  "Chrome/51.0.2704.106 Safari/537.36\r\n");
            append_string(packet, "Accept: */*;q=0.8\r\n");
            append_string(packet, "Connection: Close\r\n\r\n");

            service.write(transport, std::move(packet));
          }
          else if (event->channel_index() == 2)
          { // Sends message to server every per 3 seconds.
            udp_heartbeat.expires_from_now(std::chrono::seconds(1), 3);
            udp_heartbeat.async_wait([&service, transport](bool) { // called at network thread
              std::vector<char> packet;
              append_string(packet, "hello udp server!\n");
              service.write(transport, std::move(packet));
            });
          }

          
          transports.push_back(transport);
        }
        break;
      case YASIO_EVENT_CONNECTION_LOST:
        printf("The connection is lost(user end)!\n");
        break;
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  service.open(0); // open http client
  service.open(1, CHANNEL_UDP_SERVER); // open udp server

  udpconn_delay.expires_from_now(std::chrono::seconds(3));
  udpconn_delay.async_wait([&](bool) { // called at network thread
    printf("Open channel 2 to connect udp server.\n");
    service.open(2, CHANNEL_UDP_CLIENT); // open udp client
  });

  time_t duration = 0;
  while (true)
  {
    service.dispatch_events();
    if (duration >= 60000)
    {
      for (auto transport : transports)
        service.close(transport);
      break;
    }
    duration += 50;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::this_thread::sleep_for(std::chrono::seconds(60));
}

int main(int, char **)
{
  yasioTest();

  return 0;
}
