
#include "CLAM/Network.hxx"
#include "CLAM/PushFlowControl.hxx"
#include "CLAM/Err.hxx"
#include "CLAM/SimpleOscillator.hxx"
#include "CLAM/AudioMultiplier.hxx"
#include "CLAM/AudioOut.hxx"
#include <iostream>
#include "CLAM/AudioManager.hxx"
#include <CLAM/JACKNetworkPlayer.hxx>
#include <jack/jack.h>
#include "CLAM/OutControlSender.hxx"
#include <CLAM/XMLStorage.hxx>
#include "CLAM/Channelizer.hxx"
#include "CLAM/AudioMixer.hxx"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstdlib>
#include <cerrno>
/*#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>*/

/* Note: curl library is at /usr/include, other libs are in /usr/local/include, using symlink */
#include "curl/curl.h"
#include "curl/types.h"
#include "curl/easy.h"

//#include <sstream>

//using namespace cURLpp::Options;
#define IS_NOT_TALKING(x)	(x & NOT_TALKING)
#define IS_START_TALKING(x)	(x & START_TALKING)
#define IS_STILL_TALKING(x)	(x & STILL_TALKING)
#define IS_STOP_TALKING(x)	(x & STOP_TALKING)
#define FLOOR_FREE		-1 == channelThatHasFloor

const static double dominanceThreshold = .6;
const static double beepLength = 3.0;
const static double overlapLength = 3.0;
double currOverlapLength = 0;
struct timeval _currTime, _beepTimeDiff, _overlapStartTime;
bool isOverlapping = false;
double diffTime = 0.0;

// White Noise (or tone) Channel Identification
// Each channel has a specific tone identified with it. All channels can hear all tones.
// When a someone is speaking, only the speaker's tone is heard.


// cchien



void playNoise(CLAM::Channelizer* channels[],CLAM::Processing* tones[]){
	
	while(true){
	//No one speaking

		for (int i=0; i<4; i++)
		{	//Someone speaking
			if (IS_STILL_TALKING(channels[i]->state))
			{	for (int j=0; j<4; j++)
				{	if (j==i) CLAM::SendFloatToInControl(*(tones[j]), "Amplitude", 0.5);
					else CLAM::SendFloatToInControl(*(tones[j]), "Amplitude", 0.0);
				}
			}
	
			//No one speaking
			else
			{	for (int j=0; j<4; j++)
				{	CLAM::SendFloatToInControl(*(tones[j]), "Amplitude", 0.5);
				}
			}
		}
	}
}



/*
//PGAO
inline void playNoise (CLAM::Channelizer* channels[],CLAM::Processing* tones[]){

       while(true){
           //Nobody speaking
       	   while(IS_NOT_TALKING(channels[0]->state)&&IS_NOT_TALKING(channels[1]->state)&&
           IS_NOT_TALKING(channels[2]->state)&&IS_NOT_TALKING(channels[3]->state)){
       
               for(int i=0; i<4; i++){
                    CLAM::SendFloatToInControl(*(tones[i]), "Amplitude", 0.25);
               }
       
           }
	
           //Person speaking
	       
	   for (int i=0; i<4; i++) {
		if(IS_START_TALKING(channels[i]->state)){
			CLAM::SendFloatToInControl(*(tones[i]), "Amplitude", 0.5);
		}
               else if(IS_STILL_TALKING(channels[i]->state)){
                                                        CLAM::SendFloatToInControl(*(tones[i]), "Amplitude", 0.25);
               }
              else{
                   CLAM::SendFloatToInControl(*(tones[i]), "Amplitude", 0.0);
              }
	       
	       
           }
	}
}
*/


// Floor Stuff
int channelThatHasFloor = -1;

int error(const std::string & msg)
{
	std::cerr << msg << std::endl;
	return -1;
}

// Determines if each person is dominant or not based on their total Activity Level (how long they've been talking)
inline void calculateDominance(CLAM::Channelizer* channels[]) {
	double totalTSL = channels[0]->totalSpeakingLength + channels[1]->totalSpeakingLength + channels[2]->totalSpeakingLength + channels[3]->totalSpeakingLength;
	for(int i = 0; i < 4; i++) {
		channels[i]->totalActivityLevel = channels[i]->totalSpeakingLength / totalTSL;
		(channels[i]->totalActivityLevel >= dominanceThreshold) ? channels[i]->isDominant = true : channels[i]->isDominant = false;
	}
}


// Beeps any channels that have been overlapping for at least 5 seconds and do not have the floor
// Stops beeping after 3 seconds and starts all over again
inline void adjustAlerts(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {

	gettimeofday(&_currTime, 0x0);
	diffTime = 0.0;

	for(int i = 0; i < 4; i++) {
		// If you've been marked but haven't been beeped yet
		if(channels[i]->isGonnaGetBeeped && !channels[i]->isBeingBeeped) {
			gettimeofday(&(channels[i]->_beepStartTime),0x0);
			channels[i]->isBeingBeeped = true;
			channels[i]->isGonnaGetBeeped = false;
			//std::cout << "starting beep\n";
			CLAM::SendFloatToInControl(*(mixers[i]), "Gain 3", 1.0);
		}
		// If you're currently being beeped, make sure we don't beep you longer than X seconds
		else if (channels[i]->isBeingBeeped) {
			channels[i]->timeval_subtract(&_beepTimeDiff, &_currTime, &(channels[i]->_beepStartTime));
			diffTime = (double)_beepTimeDiff.tv_sec + (double)0.001*_beepTimeDiff.tv_usec/1000;

			// Turn off beep
			if(diffTime >= beepLength) {
				CLAM::SendFloatToInControl(*(mixers[i]), "Gain 3", 0.0);
				channels[i]->isBeingBeeped = false;
			}
		}
	}
}


// Looks at each Speaker State, updates each channel's Floor Action State
void updateFloorActions(CLAM::Channelizer* channels[]) {
	for(int i = 0; i < 4; i++) {
		if(IS_START_TALKING(channels[i]->state))
			channels[i]->floorAction = TAKE_FLOOR;
		else if (IS_STILL_TALKING(channels[i]->state))
			channels[i]->floorAction = HOLD_FLOOR;
		else if (IS_STOP_TALKING(channels[i]->state))
			channels[i]->floorAction = RELEASE_FLOOR;
		else if (IS_NOT_TALKING(channels[i]->state))
			channels[i]->floorAction = NO_FLOOR;
	}
}

// Find the number of people who are speaking now
inline int findNumSpeakers(CLAM::Channelizer* channels[]) {
	int speakers = 0;
	for(int i = 0; i < 4; i++) {
		if(IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction))
			speakers++;
	}
	return speakers;
}


inline std::string giveFloorToLeastDominantGuy(CLAM::Channelizer* channels[] ) {
	//std::cout << "giveFloorToLeastDominantGuy" << std::endl;
	short channelThatIsLeastDominant = channelThatHasFloor;
	std::ostringstream oss;

	for(int i = 0; i < 4; i++) {
		// If you're talking, we'll look at your activity levels, if you haven't been active, you get floor
		if(IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction)) {
			if(channels[i]->totalActivityLevel < channels[channelThatIsLeastDominant]->totalActivityLevel) {
				channelThatIsLeastDominant = i;
			}
		}
	}

	oss << (channelThatHasFloor+1);
	std::string output = "Channel " + oss.str() + ", you've been talking for quite some time, why don't you let Channel ";
	oss.str("");
	oss << (channelThatIsLeastDominant+1);
	output += oss.str() + " take over?";
	channelThatHasFloor = channelThatIsLeastDominant;
	return output;
}


// Looks at each Floor Action, updates the global Floor State
std::string updateFloorState(CLAM::Channelizer* channels[]) {
	std::string outputMsg;
	std::ostringstream oss;

	int numSpeakers = findNumSpeakers(channels);

	// Case 1: No one has floor, only 1 person talking
	// 	   Figure out who to give it to
	if(FLOOR_FREE) {
		int numWhoWantFloor = 0;
		short channelWhoWantsFloor = -1;
		for(int i = 0; i < 4; i++) {
			if(IS_TAKE_FLOOR(channels[i]->floorAction)) {
				channelWhoWantsFloor = i;
				numWhoWantFloor++;
			}
		}

		if(numWhoWantFloor == 1) {
			channelThatHasFloor = channelWhoWantsFloor;
			oss << (channelThatHasFloor+1);
			outputMsg = "Giving Floor to Channel " + oss.str() + "\n";
			isOverlapping = false;
		}
		else {
			// More than 1 guy started at the same time; very very rare case, deal with it later
		}
	}
	// Case 2: Someone has floor; check everyone else's status before updating anything
	else {
		// Case 2.1: Only 1 guy talking and holding the floor, most common case, let him continue
		if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 == numSpeakers)) {
			//oss << (channelThatHasFloor+1);
			//outputMsg = "Normal case, Channel " + oss.str() + " holding the floor\n";
			isOverlapping = false;
		}

		// Case 2.2: 1 guy talking and holding the floor, 1 or more guys interrupt him: BARGE IN
		// 	If there is an overlap for longer than overlapLength, mark everyone who does not 
		// 	have the floor and is talking; whoever is marked will get beeped
		else if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 != numSpeakers)) {

			if(channels[channelThatHasFloor]->isDominant)
				outputMsg = giveFloorToLeastDominantGuy(channels);

			if(!isOverlapping) {
				gettimeofday(&_overlapStartTime, 0x0);
				isOverlapping = true;
			}

			gettimeofday(&_currTime, 0x0);
			channels[channelThatHasFloor]->timeval_subtract(&_beepTimeDiff, &_currTime, &_overlapStartTime);
			diffTime = (double)_beepTimeDiff.tv_sec + (double)0.001*_beepTimeDiff.tv_usec/1000;
			currOverlapLength = diffTime;
			if(currOverlapLength >= overlapLength) {
				
				// Mark everyone who is talking that doesn't have the floor
				for (int i = 0; i < 4; i++) {
					if((i != channelThatHasFloor) && (IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction))) {
						channels[i]->isGonnaGetBeeped = true;
					}
				}
				currOverlapLength = 0.0;
				isOverlapping = false;	// TODO: may not be a good var name
			}
		}
		else if (0 == numSpeakers) {
			channelThatHasFloor = -1;
		}
	}

	return outputMsg;
}

std::string updateFloorStuff(CLAM::Channelizer* channels[], std::string prevMsg, CLAM::Processing* mixers[]) {
	std::string notifyMsg = "";
	
	calculateDominance(channels);

	adjustAlerts(channels, mixers);

	updateFloorActions(channels);

	notifyMsg = updateFloorState(channels);

	if(("" != notifyMsg) && (prevMsg != notifyMsg)) {
		std::cout << notifyMsg << std::endl;
	}
	return notifyMsg;
}

int main( int argc, char** argv )
{	
	bool netjackMode = false;
	if((argc > 1) && !strcmp(argv[1], "net"))
		netjackMode = true;
	try
	{

/*******************************************************************/
/*-----------------------------SETUP-------------------------------*/
/*******************************************************************/


		// These values will be used in some configurations, so we declare it now.
		int size = 128;
		int sampleRate = 16000;

		// We need to deploy the audio manager class in order to get audio sound.
		CLAM::AudioManager manager( sampleRate, size );
		manager.Start();

		// First of all we need to create a clam network. It isn't really complicated; just setting the name.
		CLAM::Network network;
		network.SetPlayer(new CLAM::JACKNetworkPlayer("client1"));

		try
		{
			//CLAM::XMLStorage::Restore(network, "/home/rahul/Project/MultiParty/emacspace/SNS5/windowing.clamnetwork");
			CLAM::XMLStorage::Restore(network, "/home/christine/Project/sns5/windowing.clamnetwork");
			
			//CLAM::XMLStorage::Restore(network, argv[1]);			
		}
		catch (CLAM::XmlStorageErr & e)
		{
			return error("Could not open the network file");
		}

		// TODO
		CLAM::Processing& mixer1 = network.GetProcessing("AudioMixer");
		CLAM::Processing& mixer2 = network.GetProcessing("AudioMixer_1");
		CLAM::Processing& mixer3 = network.GetProcessing("AudioMixer_2");
		CLAM::Processing& mixer4 = network.GetProcessing("AudioMixer_3");
		CLAM::SendFloatToInControl(mixer1, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer2, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer3, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer4, "Gain 3",0.0);

		//cchien pgao
		CLAM::Processing& tone1 = network.GetProcessing("SimpleOscillator");
		CLAM::Processing& tone2 = network.GetProcessing("SimpleOscillator_1");
		CLAM::Processing& tone3 = network.GetProcessing("SimpleOscillator_2");
		CLAM::Processing& tone4 = network.GetProcessing("SimpleOscillator_3");
		CLAM::Processing* tones[4] = {&tone1, &tone2, &tone3, &tone4};

		CLAM::SendFloatToInControl(*(tones[0]), "Pitch", 261.62601);
   		CLAM::SendFloatToInControl(*(tones[1]), "Pitch", 329.62799);
   		CLAM::SendFloatToInControl(*(tones[2]), "Pitch", 391.99500);
   		CLAM::SendFloatToInControl(*(tones[3]), "Pitch", 466.16400);
		
		
		CLAM::Processing& generator = network.GetProcessing("Generator");
		//CLAM::SendFloatToInControl(generator, "Amplitude", 1.0);

		CLAM::Processing& mic = network.GetProcessing("AudioSource");
		CLAM::Channelizer& myp1 = (CLAM::Channelizer&) network.GetProcessing("Channelizer");
		CLAM::Channelizer& myp2 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_1");
		CLAM::Channelizer& myp3 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_2");
		CLAM::Channelizer& myp4 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_3");

		myp1.SetPName("Channel 1");
		myp2.SetPName("Channel 2");
		myp3.SetPName("Channel 3");
		myp4.SetPName("Channel 4");		

		int winSize = mic.GetOutPort("1").GetSize();
		myp1.GetInPort("Input").SetSize(winSize);	
		myp2.GetInPort("Input").SetSize(winSize);	
		myp3.GetInPort("Input").SetSize(winSize);	
		myp4.GetInPort("Input").SetSize(winSize);	
		

		//JACK CODE
         	jack_client_t * jackClient;
         	std::string jackClientName;
		jack_status_t jackStatus;
		jackClient = jack_client_open ( "test", JackNullOption, &jackStatus );
		
		network.Start();
		
		if(netjackMode) {
			jack_connect(jackClient, "netjack:capture_1", "client1:AudioSource_1");
			jack_connect(jackClient, "netjack-01:capture_1", "client1:AudioSource_2");
			//jack_connect(jackClient, "netjack-02:capture_1", "client1:AudioSource_3");
			//jack_connect(jackClient, "netjack-03:capture_1", "client1:AudioSource_4");
		
			jack_connect(jackClient, "client1:AudioSink_1", "netjack:playback_1");
			jack_connect(jackClient, "client1:AudioSink_1", "netjack:playback_2");
			jack_connect(jackClient, "client1:AudioSink_2", "netjack-01:playback_1");
			jack_connect(jackClient, "client1:AudioSink_2", "netjack-01:playback_2");
			//jack_connect(jackClient, "client1:AudioSink_3", "netjack-02:playback_1");
			//jack_connect(jackClient, "client1:AudioSink_3", "netjack-02:playback_2");
			//jack_connect(jackClient, "client1:AudioSink_4", "netjack-03:playback_1");
			//jack_connect(jackClient, "client1:AudioSink_4", "netjack-03:playback_2");
		}
		else {
			jack_connect(jackClient, "system:capture_1", "client1:AudioSource_1");
			jack_connect(jackClient, "dev2:capture_1", "client1:AudioSource_2");
			jack_connect(jackClient, "netjack:capture_1", "client1:AudioSource_3");
			//jack_connect(jackClient, "dev4:capture_1", "client1:AudioSource_4");
		
			jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_1");
			jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_2");
			//jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_1");
			//jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_2");
			jack_connect(jackClient, "client1:AudioSink_1", "system:playback_1");
			jack_connect(jackClient, "client1:AudioSink_1", "system:playback_2");
			jack_connect(jackClient, "client1:AudioSink_3", "netjack:playback_1");
			jack_connect(jackClient, "client1:AudioSink_3", "netjack:playback_2");
		}


/*******************************************************************/
/*-----------------------SUPERVISOR STUFF--------------------------*/
/*******************************************************************/

/*
1) Detect when overlap happens: IS_TALKING(myp.state) on more than one channel
	solution steered towards increasing intelligibility

2) Who barged-in: How is natural turn-taking different from barge-in?
	while someone IS_TALKING, detect START_TALKING on another channel

3) Concept of Floor: Untill someone else starts speaking, floor belongs to last speaker
*/

		// Notify user only when something has changed, i.e. floor changes, collisions, etc.
		std::string prevMsg = "";

		CLAM::Channelizer* channels[4];
		channels[0] = &myp1;
		channels[1] = &myp2;
		channels[2] = &myp3;
		channels[3] = &myp4;

		CLAM::Processing* mixers[4];
		mixers[0] = & mixer1;
		mixers[1] = & mixer2;
		mixers[2] = & mixer3;
		mixers[3] = & mixer4;

		gettimeofday(&_currTime, 0x0);
		gettimeofday(&_beepTimeDiff, 0x0);
		
		/*std::cout << "starting curl tests\n";
		CURL *curl;
		CURLcode res;

		struct curl_httppost *formpost = NULL;
		struct curl_httppost *lastptr = NULL;
		struct curl_slist *headerlist = NULL;
		static const char buf[] = "Expect:";

		curl_global_init(CURL_GLOBAL_ALL);

		// File upload field
		curl_formadd(&formpost, 
				&lastptr, 
				CURLFORM_COPYNAME, "sendfile",
				CURLFORM_FILE, "multiPartySpeech.xml",
				CURLFORM_END);

		// Filename field
		curl_formadd(&formpost,
				&lastptr,
				CURLFORM_COPYNAME, "filename",
				CURLFORM_COPYCONTENTS, "multiPartySpeech.xml",
				CURLFORM_END);

		// Submit field, not needed really
		curl_formadd(&formpost,
				&lastptr,
				CURLFORM_COPYNAME, "submit",
				CURLFORM_COPYCONTENTS, "send",
				CURLFORM_END);

		curl = curl_easy_init();
		
		// Init header, dont think we need this really
		headerlist = curl_slist_append(headerlist, buf);
		if(curl) {
			std::cout << "inside at least...\n";
			curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:3000/main");
			curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
			res = curl_easy_perform(curl);

			curl_easy_cleanup(curl);
			curl_slist_free_all(headerlist);
		}

		std::cout << "Done curl test\n";
		exit(0);
		*/
		// TODO: make this run in terms of time, i.e. seconds
		while(1) 
		{		
			prevMsg = updateFloorStuff(channels, prevMsg, mixers);
			playNoise(channels, tones);

			//std::cout << "speaketh\t" << myp3.logEnergy << "\t" << myp4.logEnergy << std::endl;
			//CLAM::SendFloatToInControl(generator,"Amplitude",0.8);
		}
		delete [] channels;
		delete [] mixers;
		network.Stop();
		myp2.printSpeakerStats();
		myp3.printSpeakerStats();
		myp4.printSpeakerStats();
	} //try

	catch ( CLAM::Err& e )
	{
		e.Print();
		exit(-1);
	}
	catch( std::exception& e )
	{
		std::cerr << e.what() << std::endl;
		exit(-1);		
	}

	return 0;
}
