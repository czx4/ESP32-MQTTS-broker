MQTTS 3.1.1 Broker/Server for ESP32, written and tested on ESP32S3.

The main logic runs asynchronously on one thread while accepting connection requests and messages happens on the other thread. The broker uses select logic as the ESP-IDF documentation recommends using select and states that their system's poll API is implemented with select underneath.

The broker can store one retained message per subject, supports wildcards, every QOS level and will messages.

**- General Settings**

Broker can handle up to 64 concurrent clients, if enough memory is provided. If you want to adjust the initial size of hashmap containing clients, publish message information or queuing timeout it can be done in worker.cpp file. 

**- WI-FI Settings**

As the broker currently only works on WI-FI (AP capabilities to be added in future), some settings need to be set before using the broker. IP address, netmask address and gateway address can be set in includes/wificonfig.hpp. Credentials needed to connect to WI-FI can be set in includes/wificredentials.hpp. None of the strings need to be null terminated. As of now the broker only supports ipv4.

**- TLS Settings**

In order to get TLS working, private key and X.509 certificate need to be generated and provided as C literals in includes/certs.hpp file, the literals
need to have \n character at the end of each line, f.ex. :

	const unsigned char pem_prv_key[] =
	"-----BEGIN PRIVATE KEY-----\n"
	"...data...\n"
	"-----END PRIVATE KEY-----\n";
	
	const unsigned char pem_cert[] =
	"-----BEGIN CERTIFICATE-----\n"
	"...data...\n"
	"-----END CERTIFICATE-----\n";



