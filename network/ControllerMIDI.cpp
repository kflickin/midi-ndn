#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <deque>

#include <stdlib.h>
#include "RtMidi.h"

#define HEAERTBEAT_PERIOD_S 1
#define MAX_HEARTBEAT_PROBE 3

struct MIDIMessage
{
	char data[3];
};

class Controller
{
public:
	Controller(ndn::Face& face, const std::string& remoteName, const std::string& projName)
		: m_face(face)
		, m_baseName(ndn::Name("/topo-prefix/" + remoteName + "/midi-ndn/" + projName))
		, m_remoteName(remoteName)
	{
		m_connGood = false;
		m_hbCount = 0;
		heartbeatNonce = 0;
		m_face.setInterestFilter(m_baseName,
								 std::bind(&Controller::onInterest, this, _2),
								 std::bind(&Controller::onSuccess, this, _1),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });
	}

public:
	void
	addInput(MIDIMessage msg)
	{
		m_inputQueue.push_back(msg);
	}

	void
	addInput(std::string msg)
	{
		MIDIMessage midiMsg;
		for (unsigned int i = 0; i < 3; ++i)
		{
			if (i >= msg.size())
				midiMsg.data[i] = 0;
			else
				midiMsg.data[i] = msg[i];
		}
		addInput(midiMsg);
	}

	// Added for MIDI message vector

	// void
	// addInput(std::vector< unsigned char > msg)
	// {
	// 	MIDIMessage midiMsg;
	// 	for (unsigned int i = 0; i < 3; ++i)
	// 	{
	// 		if (i >= msg.size())
	// 			midiMsg.data[i] = 0;
	// 		else
	// 			msg[0] = 'a';
	// 			midiMsg.data[i] = msg[i];
	// 	}
	// 	addInput(midiMsg);
	// }

	void
	replyInterest()
	{
		// if not connected, queue will be cleared
		if (!m_connGood)
		{
			m_inputQueue.clear();
			m_interestQueue.clear();
		}

		if (!m_inputQueue.empty() && !m_interestQueue.empty())
		{
			int midiBufSize = 0;
			std::cout << "Sending Data: \n";
			while (!m_inputQueue.empty() && midiBufSize < 10){
				midiBuf[midiBufSize] = m_inputQueue.front();
				m_inputQueue.pop_front();

			
			// debug
			//std::cout << "Sending data: " << std::string(midiMsg.data, 3) << std::endl;
				std::cout << "\t";
				for (int i = 0; i < 3; ++i) {
					std::cout << " " << (int)midiBuf[midiBufSize].data[i];
				}
				std::cout << "\n";
				midiBufSize++;
			}
			ndn::Name interestName = m_interestQueue.front();
			m_interestQueue.pop_front();

			//int seqNo = interestName.get(-1).toSequenceNumber();
			sendData(interestName, (char *)midiBuf, midiBufSize*3);
		}
	}

private:
	void
	onSuccess(const ndn::Name& prefix)
	{
		std::cerr << "Prefix registered" << std::endl;
		//requestNext();
		heartbeatProbe = std::thread(&Controller::sendHeartbeat, this);
	}

	void
	onInterest(const ndn::Interest& interest)
	{
		if (!m_connGood)
		{
			// TODO: data and interest could indeed come in out of order
			std::cerr << "Connection not set up yet!?" << std::endl;
			return;
		}

		/*** send out data of keyboard input ***/

		if (m_inputQueue.empty())
		{
			// TODO: since application is realtime
			// maybe queue the interests and reply later???
			std::cerr << "Received interest but no more data to send."
					  << std::endl;
		}

		// consider out-of-order or retransmitted interest
		int seqNo = interest.getName().get(-1).toSequenceNumber();
		//if (seqNo == m_maxSeqNo)	// strict order
		if (seqNo >= m_maxSeqNo)
		{
			m_interestQueue.push_back(interest.getName());
			m_maxSeqNo = seqNo + 1;
		}
		else
		{
			std::cerr << "Dropped out-of-order packet" << std::endl;
		}
	}

	void
	onData(const ndn::Data& data)
	{
		if (data.getName().get(-1).toUri() != "heartbeat")
		{
			//std::cerr << ":P" << std::endl;
			return;
		}

		if (m_connGood)
		{
			std::cerr << "Heartbeat!" << std::endl;
			m_hbCount = 0;
			return;
		}

		// Set up connection (maybe do some checking here...)
		// But currently, "handshake" doesn't contain any data
		m_connGood = true;
		m_hbCount = 0;
		m_inputQueue.clear();
		m_interestQueue.clear();
		m_maxSeqNo = 0;	// reset seqNo tracking

		// debug
		std::cout << "Received data: "
				  << std::string(reinterpret_cast<const char*>(data.getContent().value()),
															   data.getContent().value_size())
				  << std::endl;

		std::cout << "Data name: " << data.getName().toUri() << std::endl;
	}

	
	void
	onTimeout(const ndn::Interest& interest)
	{
		// re-express interest: no need to retransmit for this case (?)
		//std::cerr << "Timeout for: " << interest << std::endl;
		//m_face.expressInterest(interest.getName(),
		//						std::bind(&Controller::onData, this, _2),
		//						std::bind(&Controller::onTimeout, this, _1));
	}
	

private:
	void
	requestNext()
	{
		m_face.expressInterest(ndn::Interest(ndn::Name(m_baseName).append("heartbeat"))
								.setMustBeFresh(true)
								.setInterestLifetime(ndn::time::seconds(HEAERTBEAT_PERIOD_S))
								.setNonce(heartbeatNonce),
								std::bind(&Controller::onData, this, _2));
								//std::bind(&Controller::onTimeout, this, _1));
		heartbeatNonce++;
		// debug
		std::cerr << "Sending out interest: " << m_baseName << std::endl;
	}

	// respond interest with data
	void
	sendData(const ndn::Name& dataName, const char *buf, size_t size)
	{
		// create data packet with the same name as interest
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(dataName);

		// prepare and assign content of the data packet
		data->setContent(reinterpret_cast<const uint8_t*>(buf), size);

		// set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(1));

		// sign data packet
		m_keyChain.sign(*data);

		// make data packet available for fetching
		m_face.put(*data);
	}

	void
	sendHeartbeat()
	{
		while (true)
		{
			m_hbCount += 1;
			requestNext();
			std::cerr << "HEARTBEAT: " << m_hbCount << std::endl;

			if (m_hbCount > MAX_HEARTBEAT_PROBE && m_connGood)
			{
				std::cerr << "Heartbeat failed! Resetting connection..." << std::endl;
				m_connGood = false;
			}

			std::this_thread::sleep_for(std::chrono::seconds(HEAERTBEAT_PERIOD_S));
		}
	}

private:
	ndn::Face& m_face;
	ndn::KeyChain m_keyChain;
	ndn::Name m_baseName;

	bool m_connGood;
	std::string m_remoteName;
	std::deque<MIDIMessage> m_inputQueue;
	std::deque<ndn::Name> m_interestQueue;
	MIDIMessage midiBuf[10]; // For multi-message sending

	int m_maxSeqNo;
	int m_hbCount;

	std::thread heartbeatProbe;
	int heartbeatNonce;

public:
	//add RtMidiIn instance to the class
	RtMidiIn *midiin;
};

// now basically what midiLoopNoBLock() is doing
void input_listener(Controller& controller)
{
	while (true)
	{
		int input = std::cin.get();
		if (input > 0)
		{
			controller.addInput("");
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

void output_sender(Controller& controller)
{
	while (true)
	{
		controller.replyInterest();
	}
}

// Beginning of RtMidi functions
void usage( void ) {
  // Error function in case of incorrect command-line
  // argument specifications.
  std::cout << "\nuseage: cmidiin <port>\n";
  std::cout << "    where port = the device to use (default = 0).\n\n";
  exit( 0 );
}

void mycallback( double deltatime, std::vector< unsigned char > *message, void */*userData*/ )
{
  unsigned int nBytes = message->size();
  for ( unsigned int i=0; i<nBytes; i++ )
    std::cout << "Byte " << i << " = " << (int)message->at(i) << ", ";
  if ( nBytes > 0 )
    std::cout << "stamp = " << deltatime << std::endl;
}

void bytecallback( double deltatime, std::vector< unsigned char > *message, void */*userData*/ )
{
  unsigned int nBytes = message->size();
}

// This function should be embedded in a try/catch block in case of
// an exception.  It offers the user a choice of MIDI ports to open.
// It returns false if there are no ports available.
bool chooseMidiPort( RtMidiIn *rtmidi );
// End of RtMidi functions

//Used in thread to get incoming midi messages
void midiLoop(char input){
	std::cin.get(input);
}

void midiLoopNoBLock(RtMidiIn *midiin, std::vector<unsigned char> message, Controller& controller){
	bool done = false;
	double stamp;
	int nBytes;
	char messageThree[3];
	while ( !done ) {
    	stamp = midiin->getMessage( &message );
    	nBytes = message.size();
    	// for (int i=0; i<nBytes; i++ ){
     //  		std::cout << "Byte " << i << " = " << (unsigned char)message[i] << ", ";
     //  	}
    	if ( nBytes >= 3 ){
    		//std::cout << std::endl;
      		for (int i=0; i<nBytes; i++){
      			if (i < 3){
      				messageThree[i] = (unsigned char)message[i];
      				//std::cout << (unsigned char)messageThree[i] << ", ";
      			}
      		}
      		//std::cout << std::endl;
      		controller.addInput(messageThree);
		}
	}
}

int main(int argc, char *argv[])
{
	/*** argument parsing ***/

	std::string remoteName = "";
	std::string projName = "tmp-proj";
	//RtMidiIn *midiin = 0; //KELLY
	std::vector<unsigned char> message; //KELLY
	
	if (argc > 1)
	{
		remoteName = argv[1];
	}
	else
	{
		std::cerr << "Must specify a remote name!" << std::endl;
		return 1;
	}

	if (argc > 2)
	{
		projName = argv[2];
	}


	try {
		// create Face instance
		ndn::Face face;

		// create server instance
		Controller controller(face, remoteName, projName);

		controller.midiin = new RtMidiIn();
		if ( chooseMidiPort( controller.midiin ) == false ) goto cleanup;
	 	//controller.midiin->setCallback( &mycallback );

     	// Don't ignore sysex, timing, or active sensing messages.
     	controller.midiin->ignoreTypes( true, true, true );

     	std::cout << "\nReading MIDI input ... press <enter> to quit.\n";

		std::thread midiThread(midiLoopNoBLock, controller.midiin, message, std::ref(controller));
		std::thread outputThread(output_sender, std::ref(controller));

		// start processing loop (it will block forever)
		face.processEvents();
	}
	catch (const std::exception& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
	}
	//DELETE RtMidiIn instance
    cleanup:
	// 	delete midiin;
	return 0;
}

bool chooseMidiPort( RtMidiIn *rtmidi )
{

  // std::cout << "\nWould you like to open a virtual input port? [y/N] ";

  std::string keyHit;
  // std::getline( std::cin, keyHit );
  // if ( keyHit == "y" ) {
  //   rtmidi->openVirtualPort();
  //   return true;
  // }
  

  std::string portName;
  unsigned int i = 0, nPorts = rtmidi->getPortCount();
  if ( nPorts == 0 ) {
    std::cout << "No input ports available!" << std::endl;
    return false;
  }

  if ( nPorts == 1 ) {
    std::cout << "\nOpening " << rtmidi->getPortName() << std::endl;
  }
  else {
    for ( i=0; i<nPorts; i++ ) {
      portName = rtmidi->getPortName(i);
      std::cout << "  Input port #" << i << ": " << portName << '\n';
    }

    do {
      std::cout << "\nChoose a port number: ";
      std::cin >> i;
    } while ( i >= nPorts );
    std::getline( std::cin, keyHit );  // used to clear out stdin
  }

  rtmidi->openPort( i );

  return true;
}
