#include <iostream>
#include <stdexcept>
#include <hue.hpp>
#include <cxxopts.hpp>
#include <backward.hpp>

/*
After research it has been discovered that the Hue REST API lacks a notification system which
makes it a suboptimal integration candidate. I would recommend integrating with a Zigbee bridge
in order to get real time notifications of state changes.

However if we are stuck with using the Hue API then there are some optimization paths that we
can pursue. If it can be shown that the Hue bridge fully and reliably supports HTTP Keep-Alive and Pipelining
then we can avoid creating a new connection upon each request.

We could also calculate the proper sleep between poll requests based on how many requests where made.
Hue recommends not exceeding more than 10 requests per second. This is not ideal since many integrations
will have more than 10 lights making a delay of at least 1 second as a lower bound.

This test application has error detection and propagation but no recovery strategy. The recovery strategy
would have to be discussed based on the integration requirements. For now any error will result in the
termination of the program via uncaught exception.


To Compile:
cd to directory with source
clang++ -std=c++11 -I. main.cpp json/jsoncpp.cpp
or
g++ -std=c++11 -I. main.cpp json/jsoncpp.cpp

Run with --help option for details on usage

Things I could have done:
1. Add make files
2. Add unit tests
3. Make a custom exception class with more details
4. Optimized the hue objects to use more Json objects
5. Cleaned up state comparisons with STL (lines 85-88 in main.cpp)
6. Groom code with my OCD comb
*/

backward::SignalHandling sh;

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);

  try {
    cxxopts::Options options(argv[0], " - Print json represented state of all lights and stream json updates of state.");
    options
      .positional_help("[host port]")
      .show_positional_help();

    options.add_options()
      ("help", "Print help")
      ("d,device", "the device name to use while requesting a username", cxxopts::value<std::string>()->default_value("my device"))
    ;

    options.parse_positional({"host", "port"});

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    std::string host = result.count("host") ? result["host"].as<std::string>() : "localhost";
    uint16_t port = result.count("port") ? result["port"].as<uint16_t>() : 8080;
    std::string device = result["device"].as<std::string>();

    hue::bridge bridge(host.c_str(), port);
    
    auto user = create_login(bridge, std::move(device));
    
    auto lights = get_lights(bridge, user);
    auto current_states = get_light_states(bridge, user, lights);

    // print current state
    std::cout << to_json(current_states) << std::endl;

    // poll for changes
    while (true) {
      auto next_states = get_light_states(bridge, user, lights);

      for (auto& light : lights) {
        auto cit = current_states.find(light);
        if (cit == current_states.end()) throw std::logic_error("light not found in current state");
        auto nit = next_states.find(light);
        if (nit == next_states.end()) throw std::logic_error("light not found in next state");
      
        auto& current_state = cit->second;
        auto& next_state = nit->second;

        auto events = get_events(current_state, next_state);
      
        // print events
        for (auto& event : events) std::cout << to_json(event) << std::endl;
      }

      std::swap(current_states, next_states);
    
      sleep(1); // naive rate limiting
    }
  } catch(const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  } catch(...) {
    std::cerr << "unknown error" << std::endl;
    return 1;
  }

  return 0;
}
