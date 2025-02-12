/**************************************************************************
    Lightspark, a free flash player implementation

    Copyright (C) 2008-2013  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef SWF_H
#define SWF_H 1

#include "compat.h"
#include <fstream>
#include <list>
#include <queue>
#include <map>
#include <unordered_set>
#include <boost/bimap.hpp>
#include <string>
#include "swftypes.h"
#include "scripting/flash/display/flashdisplay.h"
#include "scripting/flash/net/flashnet.h"
#include "scripting/flash/utils/IntervalManager.h"
#include "timer.h"
#include "memory_support.h"
#include "platforms/engineutils.h"

class uncompressing_filter;

namespace lightspark
{

class ABCVm;
class AudioManager;
class Config;
class ControlTag;
class DownloadManager;
class DisplayListTag;
class DictionaryTag;
class ExtScriptObject;
class InputThread;
class ParseThread;
class PluginManager;
class RenderThread;
class SecurityManager;
class Tag;
class ApplicationDomain;
class ASWorker;
class WorkerDomain;
class SecurityDomain;
class Class_inherit;
class FontTag;
class SoundTransform;

class RootMovieClip: public MovieClip
{
friend class ParseThread;
protected:
	URLInfo origin;
private:
	bool parsingIsFailed;
	RGB Background;
	Spinlock dictSpinlock;
	std::list < DictionaryTag* > dictionary;
	std::list< std::pair<tiny_string, DictionaryTag*> > classesToBeBound;
	std::map < tiny_string,FontTag* > embeddedfonts;
	std::map < uint32_t,FontTag* > embeddedfontsByID;

	//frameSize and frameRate are valid only after the header has been parsed
	RECT frameSize;
	float frameRate;
	URLInfo baseURL;
	/* those are private because you shouldn't call mainClip->*,
	 * but mainClip->getStage()->* instead.
	 */
	void initFrame();
	void advanceFrame();
	ACQUIRE_RELEASE_FLAG(finishedLoading);
public:
	RootMovieClip(_NR<LoaderInfo> li, _NR<ApplicationDomain> appDomain, _NR<SecurityDomain> secDomain, Class_base* c);
	~RootMovieClip();
	bool destruct();
	bool hasFinishedLoading() { return ACQUIRE_READ(finishedLoading); }
	uint32_t version;
	uint32_t fileLength;
	bool hasSymbolClass;
	bool usesActionScript3;
	RGB getBackground();
	void setBackground(const RGB& bg);
	void setFrameSize(const RECT& f);
	RECT getFrameSize() const;
	float getFrameRate() const;
	void setFrameRate(float f);
	void addToDictionary(DictionaryTag* r);
	DictionaryTag* dictionaryLookup(int id);
	DictionaryTag* dictionaryLookupByName(uint32_t nameID);
	void labelCurrentFrame(const STRING& name);
	void commitFrame(bool another);
	void revertFrame();
	void parsingFailed();
	bool boundsRect(number_t& xmin, number_t& xmax, number_t& ymin, number_t& ymax) const;
	void DLL_PUBLIC setOrigin(const tiny_string& u, const tiny_string& filename="");
	URLInfo& getOrigin() { return origin; }
	void DLL_PUBLIC setBaseURL(const tiny_string& url);
	const URLInfo& getBaseURL();
/*	ASObject* getVariableByQName(const tiny_string& name, const tiny_string& ns);
	void setVariableByQName(const tiny_string& name, const tiny_string& ns, ASObject* o);
	void setVariableByMultiname(multiname& name, asAtom o);
	void setVariableByString(const std::string& s, ASObject* o);*/
	static RootMovieClip* getInstance(_NR<LoaderInfo> li, _R<ApplicationDomain> appDomain, _R<SecurityDomain> secDomain);
	/*
	 * The application domain for this clip
	 */
	_NR<ApplicationDomain> applicationDomain;
	/*
	 * The security domain for this clip
	 */
	_NR<SecurityDomain> securityDomain;
	//DisplayObject interface
	_NR<RootMovieClip> getRoot();
	void addBinding(const tiny_string& name, DictionaryTag *tag);
	void bindClass(const QName &classname, Class_inherit* cls);
	void checkBinding(DictionaryTag* tag);
	void registerEmbeddedFont(const tiny_string fontname, FontTag *tag);
	FontTag* getEmbeddedFont(const tiny_string fontname) const;
	FontTag* getEmbeddedFontByID(uint32_t fontID) const;
	void setupAVM1RootMovie();
};

class ThreadProfile
{
private:
	/* ThreadProfile cannot be copied because Mutex cannot */
	ThreadProfile(const ThreadProfile&) { assert(false); }
	Mutex mutex;
	class ProfilingData
	{
	public:
		uint32_t index;
		uint32_t timing;
		std::string tag;
 		ProfilingData(uint32_t i, uint32_t t):index(i),timing(t){}
	};
	std::deque<ProfilingData> data;
	RGB color;
	int32_t len;
	uint32_t tickCount;
	EngineData* engineData;
public:
	ThreadProfile(const RGB& c,uint32_t l,EngineData* _engineData):color(c),len(l),tickCount(0),engineData(_engineData){}
	void accountTime(uint32_t time);
	void setTag(const std::string& tag);
	void tick();
	void plot(uint32_t max, cairo_t *cr);
};

class SystemState: public ITickJob, public InvalidateQueue
{
private:
	class EngineCreator: public IThreadJob
	{
	public:
		void execute();
		void threadAbort();
		void jobFence() { delete this; }
	};
	friend class SystemState::EngineCreator;
	ThreadPool* threadPool;
	ThreadPool* downloadThreadPool;
	TimerThread* timerThread;
	TimerThread* frameTimerThread;
	Semaphore terminated;
	float renderRate;
	bool error;
	bool shutdown;
	RenderThread* renderThread;
	InputThread* inputThread;
	EngineData* engineData;
	Thread* mainThread;
	void startRenderTicks();
	Mutex rootMutex;
	/**
		Create the rendering and input engines

		@pre engine and useAVM2 are known
	*/
	void createEngines();

	void launchGnash();
	/**
	  	Destroys all the engines used in lightspark: timer, thread pool, vm...
	*/
	void stopEngines();

	static void delayedCreation(SystemState* sys);
	void delayedStopping();

	/* dumpedSWFPathAvailable is signaled after dumpedSWFPath has been set */
	Semaphore dumpedSWFPathAvailable;
	tiny_string dumpedSWFPath;

	//Data for handling Gnash fallback
	enum VMVERSION { VMNONE=0, AVM1, AVM2 };
	VMVERSION vmVersion;
#ifdef _WIN32
	HANDLE childPid;
#else
	GPid childPid;
#endif

	//shared null, undefined, true and false instances
	_NR<Null> null;
	_NR<Undefined> undefined;
	_NR<Boolean> trueRef;
	_NR<Boolean> falseRef;
	Class_base* objClassRef;

	//Parameters/FlashVars
	_NR<ASObject> parameters;
	void setParameters(_R<ASObject> p);
	/*
	   	Used to keep a copy of the FlashVars, it's useful when gnash fallback is used
	*/
	std::string rawParameters;

	//Cookies for Gnash fallback
	std::string rawCookies;
	char* cookiesFileName;

	URLInfo url;
	Spinlock profileDataSpinlock;

	Mutex mutexFrameListeners;
	std::set<_R<DisplayObject>> frameListeners;
	/*
	   The head of the invalidate queue
	*/
	_NR<DisplayObject> invalidateQueueHead;
	/*
	   The tail of the invalidate queue
	*/
	_NR<DisplayObject> invalidateQueueTail;
	/*
	   The lock for the invalidate queue
	*/
	Spinlock invalidateQueueLock;
#ifdef PROFILING_SUPPORT
	/*
	   Output file for the profiling data
	*/
	tiny_string profOut;
#endif
#ifdef MEMORY_USAGE_PROFILING
	mutable Mutex memoryAccountsMutex;
	std::list<MemoryAccount> memoryAccounts;
#endif
	/*
	 * Pooling support
	 */
	mutable Mutex poolMutex;
	boost::bimap<tiny_string, uint32_t> uniqueStringMap;
	uint32_t lastUsedStringId;
	boost::bimap<nsNameAndKindImpl, uint32_t> uniqueNamespaceMap;
	//This needs to be atomic because it's decremented without the mutex held
	ATOMIC_INT32(lastUsedNamespaceId);
	
	Mutex mainsignalMutex;
	Cond mainsignalCond;
	void systemFinalize();
public:
	void setURL(const tiny_string& url) DLL_PUBLIC;
	tiny_string getDumpedSWFPath() const { return dumpedSWFPath;}

	//Interative analysis flags
	bool showProfilingData;
	bool standalone;
	//Flash for execution mode
	enum FLASH_MODE { FLASH=0, AIR, AVMPLUS };
	const FLASH_MODE flashMode;
	uint32_t swffilesize;
	asAtom nanAtom;
	// the global object for AVM1
	Global* avm1global;
	// Error types used to decide when to exit, extend as a bitmap
	enum ERROR_TYPE { ERROR_NONE    = 0x0000,
			  ERROR_PARSING = 0x0001,
			  ERROR_OTHER   = 0x8000,
			  ERROR_ANY     = 0xFFFF };
	std::string errorCause;
	void setError(const std::string& c, ERROR_TYPE type=ERROR_OTHER);
	bool hasError() { return error; }
	std::string& getErrorCause() { return errorCause; }
	bool shouldTerminate() const;
	bool isShuttingDown() const DLL_PUBLIC;
	bool isOnError() const DLL_PUBLIC;
	void setShutdownFlag() DLL_PUBLIC;
	void tick();
	void tickFence();
	RenderThread* getRenderThread() const { return renderThread; }
	InputThread* getInputThread() const { return inputThread; }
	void setParamsAndEngine(EngineData* e, bool s) DLL_PUBLIC;
	void setDownloadedPath(const tiny_string& p) DLL_PUBLIC;
	void needsAVM2(bool n);
	void stageCoordinateMapping(uint32_t windowWidth, uint32_t windowHeight, int& offsetX, int& offsetY, float& scaleX, float& scaleY);
	void windowToStageCoordinates(int windowX, int windowY, int& stageX, int& stageY);

	/**
	 * Be careful, SystemState constructor does some global initialization that must be done
	 * before any other thread gets started
	 * \param fileSize The size of the SWF being parsed, if known
	 * \param mode FLASH or AIR
	 */
	SystemState(uint32_t fileSize, FLASH_MODE mode) DLL_PUBLIC;
	~SystemState();
	/* Stop engines, threads and free classes and objects.
	 * This call will decRef this object in the end,
	 * thus destroy() may cause a 'delete this'.
	 */
	void destroy() DLL_PUBLIC;
	
	//Performance profiling
	ThreadProfile* allocateProfiler(const RGB& color);
	std::list<ThreadProfile*> profilingData;
	
	inline Null* getNullRef() const
	{
		return null.getPtr();
	}
	
	inline Undefined* getUndefinedRef() const
	{
		return undefined.getPtr();
	}
	
	inline Boolean* getTrueRef() const
	{
		return trueRef.getPtr();
	}
	
	inline Boolean* getFalseRef() const
	{
		return falseRef.getPtr();
	}

	inline Class_base* getObjectClassRef() const
	{
		return objClassRef;
	}

	RootMovieClip* mainClip;
	Stage* stage;
	ABCVm* currentVm;

	AudioManager* audioManager;

	//Application starting time in milliseconds
	uint64_t startTime;

	//Classes set. They own one reference to each class/template
	std::set<Class_base*> customClasses;
	//This is an array of fixed size, we can avoid using std::vector
	Class_base** builtinClasses;
	std::map<QName, Template_base*> templates;

	std::map<QName,std::unordered_set<uint32_t>*> customclassoverriddenmethods;
	
	//Flags for command line options
	bool useInterpreter;
	bool useFastInterpreter;
	bool useJit;
	ERROR_TYPE exitOnError;

	//Parameters/FlashVars
	void parseParametersFromFile(const char* f) DLL_PUBLIC;
	void parseParametersFromFlashvars(const char* vars) DLL_PUBLIC;
	void parseParametersFromURL(const URLInfo& url) DLL_PUBLIC;
	static void parseParametersFromURLIntoObject(const URLInfo& url, _R<ASObject> outParams);
	_NR<ASObject> getParameters() const;

	//Cookies management (HTTP downloads and Gnash fallback)
	void setCookies(const char* c) DLL_PUBLIC;
	const std::string& getCookies();

	//Interfaces to the internal thread pool and timer thread
	void addJob(IThreadJob* j) DLL_PUBLIC;
	// downloaders may be executed from inside a job from the main threadpool,
	// so we use a second threadpool for them, to avoid deadlocks
	void addDownloadJob(IThreadJob* j) DLL_PUBLIC;
	void addTick(uint32_t tickTime, ITickJob* job);
	void addFrameTick(uint32_t tickTime, ITickJob* job);
	void addWait(uint32_t waitTime, ITickJob* job);
	void removeJob(ITickJob* job);

	void setRenderRate(float rate);
	float getRenderRate();

	/*
	 * The application domain for the system
	 */
	_NR<ApplicationDomain> systemDomain;

	_NR<ASWorker> worker;
	_NR<WorkerDomain> workerDomain;
	bool singleworker;
	Mutex workerMutex;
	void addWorker(ASWorker* w);
	void removeWorker(ASWorker* w);

	//Stuff to be done once for process and not for plugin instance
	static void staticInit() DLL_PUBLIC;
	static void staticDeinit() DLL_PUBLIC;

	DownloadManager* downloadManager;
	IntervalManager* intervalManager;
	SecurityManager* securityManager;
	ExtScriptObject* extScriptObject;

	enum SCALE_MODE { EXACT_FIT=0, NO_BORDER=1, NO_SCALE=2, SHOW_ALL=3 };
	SCALE_MODE scaleMode;
	
	//Static AS class properties
	//TODO: Those should be different for each security domain
	//NAMING: static$CLASSNAME$$PROPERTYNAME$
	//	NetConnection
	ObjectEncoding::ENCODING staticNetConnectionDefaultObjectEncoding;
	ObjectEncoding::ENCODING staticByteArrayDefaultObjectEncoding;
	ObjectEncoding::ENCODING staticSharedObjectDefaultObjectEncoding;
	bool staticSharedObjectPreventBackup;
	
	//enterFrame event management
	void registerFrameListener(_R<DisplayObject> clip);
	void unregisterFrameListener(_R<DisplayObject> clip);

	//tags management
	void registerTag(Tag* t);

	//Invalidation queue management
	void addToInvalidateQueue(_R<DisplayObject> d);
	void flushInvalidationQueue();

	//Resize support
	void resizeCompleted();

	/*
	 * Support for class aliases in AMF3 serialization
	 */
	std::map<tiny_string, _R<Class_base> > aliasMap;
#ifdef PROFILING_SUPPORT
	void setProfilingOutput(const tiny_string& t) DLL_PUBLIC;
	const tiny_string& getProfilingOutput() const;
	std::vector<ABCContext*> contextes;
	void saveProfilingInformation();
#endif
	MemoryAccount* allocateMemoryAccount(const tiny_string& name) DLL_PUBLIC;
	MemoryAccount* unaccountedMemory;
	MemoryAccount* tagsMemory;
	MemoryAccount* stringMemory;
	MemoryAccount* textTokenMemory;
	MemoryAccount* shapeTokenMemory;
	MemoryAccount* morphShapeTokenMemory;
	MemoryAccount* bitmapTokenMemory;
	MemoryAccount* spriteTokenMemory;
#ifdef MEMORY_USAGE_PROFILING
	void saveMemoryUsageInformation(std::ofstream& out, int snapshotCount) const;
#endif
	/*
	 * Pooling support
	 */
	uint32_t getUniqueStringId(const tiny_string& s);
	const tiny_string& getStringFromUniqueId(uint32_t id) const;
	/*
	 * Looks for the given nsNameAndKindImpl in the map.
	 * If not present it will be created with hintedId as it's id.
	 * The namespace id and the baseId are returned by reference.
	 */
	void getUniqueNamespaceId(const nsNameAndKindImpl& s, uint32_t hintedId, uint32_t& nsId, uint32_t& baseId);
	/*
	 * Looks for the given nsNameAndKindImpl in the map.
	 * If not present it will be create with an id chosen internally.
	 * The namespace id and the baseId are returned by reference.
	 */
	void getUniqueNamespaceId(const nsNameAndKindImpl& s, uint32_t& nsId, uint32_t& baseId);
	const nsNameAndKindImpl& getNamespaceFromUniqueId(uint32_t id) const;

	//Opening web pages
	void openPageInBrowser(const tiny_string& url, const tiny_string& window);

	void showMouseCursor(bool visible);
	void waitRendering() DLL_PUBLIC;
	EngineData* getEngineData() { return engineData;}
	uint32_t getSwfVersion();

	// these methods ensure that externalcallevents are executed as soon and as fast as possible
	// the ppapi plugin needs this because external call events are blocking the main plugin thread 
	// so we need to make sure external call events are executed even if another plugin method is currently running in another thread
	void checkExternalCallEvent() DLL_PUBLIC;
	void waitMainSignal() DLL_PUBLIC;
	void sendMainSignal() DLL_PUBLIC;

	void dumpStacktrace();
	
	// static class properties are named static_<classname>_<propertyname>
	_NR<SoundTransform> static_SoundMixer_soundTransform;
	int static_SoundMixer_bufferTime;
	_NR<ASObject> static_ObjectEncoding_dynamicPropertyWriter;

	ACQUIRE_RELEASE_FLAG(isinitialized);
	Mutex initializedMutex;
	Cond initializedCond;
	void waitInitialized();
};

class ParseThread: public IThreadJob
{
public:
	int version;
	// Parse an object from stream. The type is detected
	// automatically. After parsing the new object is available
	// from getParsedObject().
	ParseThread(std::istream& in, _R<ApplicationDomain> appDomain, _R<SecurityDomain> secDomain, Loader *loader, tiny_string url) DLL_PUBLIC;
	// Parse a clip from stream into root. The stream must be an
	// SWF file.
	ParseThread(std::istream& in, RootMovieClip *root) DLL_PUBLIC;
	~ParseThread();
	FILE_TYPE getFileType() const { return fileType; }
        _NR<DisplayObject> getParsedObject();
	RootMovieClip* getRootMovie() const;
	static FILE_TYPE recognizeFile(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4);
	void execute();
	_NR<ApplicationDomain> applicationDomain;
	_NR<SecurityDomain> securityDomain;
private:
	std::istream& f;
	uncompressing_filter* uncompressingFilter;
	std::streambuf* backend;
	Loader *loader;
	_NR<DisplayObject> parsedObject;
	Spinlock objectSpinlock;
	tiny_string url;
	FILE_TYPE fileType;
	void threadAbort();
	void jobFence() {}
	void parseSWFHeader(RootMovieClip *root, UI8 ver);
	void parseSWF(UI8 ver);
	void parseBitmap();
	void setRootMovie(RootMovieClip *root);
};

/* Returns the thread-specific SystemState */
SystemState* getSys() DLL_PUBLIC;
/* Set thread-specific SystemState to be returned by getSys() */
void setTLSSys(SystemState* sys) DLL_PUBLIC;

ParseThread* getParseThread();
/* Returns the thread-specific SystemState */
ASWorker* getWorker() DLL_PUBLIC;
void setTLSWorker(ASWorker* worker) DLL_PUBLIC;

}
#endif /* SWF_H */
