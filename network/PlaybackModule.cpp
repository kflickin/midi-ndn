/********************************

Plays back MIDI message received
Acts as a producer and consumer at the same time.

This part is a.k.a. "NDN Module"

Note:
For received interest packet:
					-3		-2 		-1
(topology-prefix)/<user>/midi-ndn/<proj_name>
if we add in device then it's -4

For received data packet / sent interest packet:
user position = user position in received interest - 1 = -4

********************************/

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <iostream>
#include <string>
#include <map>

#include <unistd.h>
#include <string.h>

class PlaybackModule
{
public:
	PlaybackModule(ndn::Face& face, const std::string& hostname, const std::string& projname)
		: m_face(face)
		, m_baseName(ndn::Name("/topo-prefix/" + hostname + "/midi-ndn/" + projname))
	{
		m_face.setInterestFilter(m_baseName,
								 std::bind(&PlaybackModule::onInterest, this, _2),
								 std::bind([] {
									std::cerr << "Prefix registered" << std::endl;
								 }),
								 [] (const ndn::Name& prefix, const std::string& reason) {
									std::cerr << "Failed to register prefix: " << reason << std::endl;
								 });
	}

private:
	void
	onInterest(const ndn::Interest& interest)
	{
		/*** check if connection already exist ***/

		std::string remoteName = interest.getName().get(-3).toUri();;

		if (m_lookup.count(remoteName) > 0)
		{
			std::cerr << "connection request dropped: " << interest << std::endl;
			return;
		}

		/*** accept new connection ***/

		m_lookup[remoteName] = 0;

		std::cerr << "connection accepted: " << interest << std::endl;

		/*** respond to connection request ***/

		// create data packet with the same name as interest
		std::shared_ptr<ndn::Data> data = std::make_shared<ndn::Data>(interest.getName());

		// prepare and assign content of the data packet
		std::string content = "ACCEPTED";
		data->setContent(reinterpret_cast<const uint8_t*>(content.c_str()), content.size());

		// set metainfo parameters
		data->setFreshnessPeriod(ndn::time::seconds(10));

		// sign data packet
		m_keyChain.sign(*data);

		// make data packet available for fetching
		m_face.put(*data);

		/*** start sending out interest for next seq ***/

		requestNext(remoteName);
	}

	void
	onData(const ndn::Data& data)
	{
		int seqNo = data.getName().get(-1).toSequenceNumber();
		std::string remoteName = data.getName().get(-4).toUri();

		// CHECKPOINT 1: connection actually exist
		if (m_lookup.count(remoteName) == 0)
		{
			// the connection doesn't exist!!
			std::cerr << "Connection for remote user \""
					  << remoteName << "\" doesn't exist!"
					  << std::endl;
			return;
		}

		// CHECKPOINT 2: sequence number agrees
		if (m_lookup[remoteName] != seqNo)
		{
			// behavior yet to be defined
			std::cerr << "Sequence number out of order --> "
					  << "sent: " << m_lookup[remoteName]
					  << "  rcvd: " << seqNo
					  << std::endl;
		}

		// CHECKPOINT 3: data is in correct format
		char buffer[3];
		if (data.getContent().value_size() != 3)
		{
			// incorrect data format
			// behavior yet to be defined
			std::cerr << "Incorrect data format: len = "
					  << data.getContent().value_size()
					  << " (expected 3)"
					  << std::endl;
		}

		/**
		 * Starting here all check points are passed
		 * copy data and increment sequence number
		 */
		memcpy(buffer, data.getContent().value(), 3);
		++m_lookup[remoteName];

		// debug
		std::cout << "Received data:";
		for (int i = 0; i < 3; ++i)
		{
			std::cout << " " << (int)buffer[i];
		}
		std::cout << std::endl;

		// currently using a special message to shutdown... 
		if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0)
		{
			std::cerr << "Deleting table entry of: " << remoteName << std::endl;
			m_lookup.erase(remoteName);
			return;
		}

		/**
		 * TODO: process data
		 */
		
		requestNext(remoteName);
	}

	void
	onTimeout(const ndn::Interest& interest)
	{
		// re-express interest
		std::cerr << "Timeout for: " << interest << std::endl;
		m_face.expressInterest(interest.getName(),
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));
	}

private:
	void
	requestNext(std::string remoteName)
	{
		if (m_lookup.count(remoteName) == 0)
		{
			// weird, maybe connection is closed or something
			// or people trying to be malicious (LOL)
			std::cerr << "Attempted to request from non-existent remote: "
					  << remoteName
					  << " - DROPPED"
					  << std::endl;
			return;
		}

		int nextSeqNo = m_lookup[remoteName];
		ndn::Name nextName = ndn::Name(m_baseName).appendSequenceNumber(nextSeqNo);
		m_face.expressInterest(ndn::Interest(nextName).setMustBeFresh(true),
								std::bind(&PlaybackModule::onData, this, _2),
								std::bind(&PlaybackModule::onTimeout, this, _1));
		//m_lookup[remoteName] = ++nextSeqNo;	// done in receiving data

		// debug
		std::cerr << "Sending out interest: " << nextName << std::endl;
	}

private:
	ndn::Face& m_face;
	ndn::KeyChain m_keyChain;
	ndn::Name m_baseName;

	// maps foreign hostname (remoteName) to its next seq number
	std::map<std::string, int> m_lookup;
};

int main(int argc, char *argv[])
{
	// get unique user name
	char namebuf[64];
	gethostname(namebuf, 64);
	std::string hostname = namebuf;

	// get project name: default is tmp-proj
	std::string projname = "tmp-proj";
	if (argc > 1)
	{
		projname = argv[1];
	}

	try {
		// create Face instance
		ndn::Face face;

		// create server instance
		PlaybackModule ndnModule(face, hostname, projname);

		// start processing loop (it will block forever)
		face.processEvents();
	}
	catch (const std::exception& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
	}

	return 0;
}
