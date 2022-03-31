#include <thread>
#include <zmq.hpp>
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include "json.hpp"
using json = nlohmann::json;


static int s_interrupted = 0;
static void s_signal_handler (int signal_value)
{
    if(s_interrupted == 0)
    {
        std::cout << "sighandler" << std::endl;
        s_interrupted = 1;

        zmq::context_t context(1);
        zmq::socket_t socket(context, ZMQ_PAIR);
        socket.connect("ipc://killmebaby");
        zmq::message_t msg;
        memcpy(msg.data(),"0", 1);
        socket.send(msg);
    }
}

// Setup signal handler to quit the program
static void s_catch_signals (void)
{
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}


const std::string TOPIC = "Bittrex";

void startPublisher()
{
    zmq::context_t zmq_context(1);
    zmq::socket_t zmq_socket(zmq_context, ZMQ_PUB);
    zmq_socket.bind("ipc://localsock");
    usleep(100000); // Sending message too fast after connexion will result in dropped message
    zmq::message_t msg(3);
    zmq::message_t topic(4);
    for(int i = 0; i < 10; i++) {

        memcpy(topic.data(), TOPIC.data(), TOPIC.size()); // <= Change your topic message here
        memcpy(msg.data(), "abc", 3);
        try {
            zmq_socket.send(topic, ZMQ_SNDMORE); 
            zmq_socket.send(msg); 
        } catch(zmq::error_t &e) {
            std::cout << e.what() << std::endl;
        }
        msg.rebuild(3);
        topic.rebuild(4);
        usleep(1); // Temporisation between message; not necessary
    }
}

void startSubscriber()
{
    zmq::context_t zmq_context(1);
    zmq::socket_t zmq_socket(zmq_context, ZMQ_SUB);
    // zmq_socket.connect("tcp://192.168.20.152:5600");
    zmq_socket.connect("tcp://192.168.20.104:4242");

    zmq::socket_t killer_socket(zmq_context, ZMQ_PAIR); // This socket is used to terminate the loop on a signal
    killer_socket.bind("ipc://killmebaby");

    zmq_socket.setsockopt(ZMQ_SUBSCRIBE, TOPIC.c_str(), TOPIC.length()); // Subscribe to any topic you want here
    zmq::pollitem_t items [] = {
        { zmq_socket, 0, ZMQ_POLLIN, 0 },
        { killer_socket, 0, ZMQ_POLLIN, 0 }
    };
    while(true)
    {
        int rc = 0;
        zmq::message_t topic;
        zmq::message_t msg;
        zmq::poll (&items [0], 2, -1);

        if (items [0].revents & ZMQ_POLLIN)
        {
            std::cout << "waiting on recv..." << std::endl;
            rc = zmq_socket.recv(&topic, ZMQ_RCVMORE);  // Works fine
            rc = zmq_socket.recv(&msg) && rc;
            if(rc > 0) // Do no print trace when recv return from timeout
            {
                std::cout << "topic: \"" << std::string(static_cast<char*>(topic.data()), topic.size()) << "\"" << std::endl;
                std::cout << "msg: \"" << std::string(static_cast<char*>(msg.data()), msg.size()) << "\"" << std::endl;
                // start parsing msg as json
                auto data = json::parse(std::string(static_cast<char*>(msg.data()), msg.size()));
                // explicit conversion to string
                // std::cout << "updateid: " << data["update"]["id"] << std::endl;
                std::string s = data.dump();


                // Start processing the msg

                // {"action": "PLACE_ORDER", "symbol": "WAVES-BTC", "side": "SELL", "ordertype": "LIMIT", "orderQty": 3.0, "price": 54.44534, "orderId":"fc9bf4a3-7a2a-4673-9e64-7b4eb9562a11"}
                if (data["action"] == "PLACE_ORDER") {
                    std::string symbol = data["symbol"];
                    std::string side = data["side"];
                    std::string ordertype = data["ordertype"];
                    std::string orderId = std::string();
                    long orderQty_raw = data["orderQty"];
                    double price_raw = data["price"];

                    if (data.contains("orderId")) {
                        orderId = data["orderId"];
                    }
                    // long orderQty = strtol(orderQty_raw.c_str(), nullptr, 10);
                    // double price = strtod(price_raw.c_str(), nullptr);

                    std::cout << "PLACE ORDER: " << orderId << std::endl;

                }
                else if (data["action"] == "CANCEL_ORDER") {
                    std::string orderId = data["orderId"];
                }

            }
        }
        else if (items [1].revents & ZMQ_POLLIN)
        {
            if(s_interrupted == 1)
            {
                std::cout << "break" << std::endl;
                break;
            }
        }
    }
}

int main() {
    s_catch_signals();
    // run = true;
    std::thread t_sub(startSubscriber);
    sleep(1); // Slow joiner in ZMQ PUB/SUB pattern
    std::thread t_pub(startPublisher);
    t_pub.join();
    t_sub.join();
}