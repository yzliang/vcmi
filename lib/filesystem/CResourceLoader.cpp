#include "StdInc.h"
#include "CResourceLoader.h"
#include "CFileInfo.h"
#include "CLodArchiveLoader.h"
#include "CFilesystemLoader.h"
#include "CMappedFileLoader.h"

//For filesystem initialization
#include "../JsonNode.h"
#include "../GameConstants.h"
#include "../VCMIDirs.h"
#include "../CStopWatch.h"

CResourceLoader * CResourceHandler::resourceLoader = nullptr;
CResourceLoader * CResourceHandler::initialLoader = nullptr;

ResourceID::ResourceID()
    :type(EResType::OTHER)
{
}

ResourceID::ResourceID(std::string name)
{
	CFileInfo info(std::move(name));
	setName(info.getStem());
	setType(info.getType());
}

ResourceID::ResourceID(std::string name, EResType::Type type)
{
	setName(std::move(name));
	setType(type);
}

ResourceID::ResourceID(const std::string & prefix, const std::string & name, EResType::Type type)
{
	this->name = name;

	size_t dotPos = this->name.find_last_of("/.");

	if(dotPos != std::string::npos && this->name[dotPos] == '.')
		this->name.erase(dotPos);

	this->name = prefix + this->name;
	setType(type);
}

std::string ResourceID::getName() const
{
	return name;
}

EResType::Type ResourceID::getType() const
{
	return type;
}

void ResourceID::setName(std::string name)
{
	this->name = std::move(name);

	size_t dotPos = this->name.find_last_of("/.");

	if(dotPos != std::string::npos && this->name[dotPos] == '.')
		this->name.erase(dotPos);

	// strangely enough but this line takes 40-50% of filesystem loading time
	boost::to_upper(this->name);
}

void ResourceID::setType(EResType::Type type)
{
	this->type = type;
}

CResourceLoader::CResourceLoader()
{
}

std::unique_ptr<CInputStream> CResourceLoader::load(const ResourceID & resourceIdent) const
{
	auto resource = resources.find(resourceIdent);

	if(resource == resources.end())
	{
		throw std::runtime_error("Resource with name " + resourceIdent.getName() + " and type "
			+ EResTypeHelper::getEResTypeAsString(resourceIdent.getType()) + " wasn't found.");
	}

	// get the last added resource(most overriden)
	const ResourceLocator & locator = resource->second.back();

	// load the resource and return it
	return locator.getLoader()->load(locator.getResourceName());
}

std::pair<std::unique_ptr<ui8[]>, ui64> CResourceLoader::loadData(const ResourceID & resourceIdent) const
{
	auto stream = load(resourceIdent);
	std::unique_ptr<ui8[]> data(new ui8[stream->getSize()]);
	size_t readSize = stream->read(data.get(), stream->getSize());

	assert(readSize == stream->getSize());
	return std::make_pair(std::move(data), stream->getSize());
}

ResourceLocator CResourceLoader::getResource(const ResourceID & resourceIdent) const
{
	auto resource = resources.find(resourceIdent);

	if (resource == resources.end())
		return ResourceLocator(nullptr, "");
	return resource->second.back();
}

const std::vector<ResourceLocator> & CResourceLoader::getResourcesWithName(const ResourceID & resourceIdent) const
{
	static const std::vector<ResourceLocator> emptyList;
	auto resource = resources.find(resourceIdent);

	if (resource == resources.end())
		return emptyList;
	return resource->second;
}


std::string CResourceLoader::getResourceName(const ResourceID & resourceIdent) const
{
	auto locator = getResource(resourceIdent);
	if (locator.getLoader())
		return locator.getLoader()->getFullName(locator.getResourceName());
	return "";
}

bool CResourceLoader::existsResource(const ResourceID & resourceIdent) const
{
	return resources.find(resourceIdent) != resources.end();
}

bool CResourceLoader::createResource(std::string URI, bool update)
{
	std::string filename = URI;
	boost::to_upper(URI);
	for (auto & entry : boost::adaptors::reverse(loaders))
	{
		if (entry.writeable && boost::algorithm::starts_with(URI, entry.prefix))
		{
			// remove loader prefix from filename
			filename = filename.substr(entry.prefix.size());
			if (!entry.loader->createEntry(filename))
				continue;

			resources[ResourceID(URI)].push_back(ResourceLocator(entry.loader.get(), filename));

			// Check if resource was created successfully. Possible reasons for this to fail
			// a) loader failed to create resource (e.g. read-only FS)
			// b) in update mode, call with filename that does not exists
			assert(load(ResourceID(URI)));

			return true;
		}
	}
	return false;
}

void CResourceLoader::addLoader(std::string mountPoint, shared_ptr<ISimpleResourceLoader> loader, bool writeable)
{
	LoaderEntry loaderEntry;
	loaderEntry.loader = loader;
	loaderEntry.prefix = mountPoint;
	loaderEntry.writeable = writeable;
	loaders.push_back(loaderEntry);

	// Get entries and add them to the resources list
	const std::unordered_map<ResourceID, std::string> & entries = loader->getEntries();

	boost::to_upper(mountPoint);

	for (auto & entry : entries)
	{
		// Create identifier and locator and add them to the resources list
		ResourceID ident(mountPoint, entry.first.getName(), entry.first.getType());
		ResourceLocator locator(loader.get(), entry.second);

		resources[ident].push_back(locator);
	}
}

CResourceLoader * CResourceHandler::get()
{
	if(resourceLoader != nullptr)
	{
		return resourceLoader;
	}
	else
	{
		std::stringstream string;
		string << "Error: Resource loader wasn't initialized. "
			   << "Make sure that you set one via CResourceLoaderFactory::initialize";
		throw std::runtime_error(string.str());
	}
}

void CResourceHandler::clear()
{
	delete resourceLoader;
	delete initialLoader;
}

//void CResourceLoaderFactory::setInstance(CResourceLoader * resourceLoader)
//{
//	CResourceLoaderFactory::resourceLoader = resourceLoader;
//}

ResourceLocator::ResourceLocator(ISimpleResourceLoader * loader, const std::string & resourceName)
			: loader(loader), resourceName(resourceName)
{

}

ISimpleResourceLoader * ResourceLocator::getLoader() const
{
	return loader;
}

std::string ResourceLocator::getResourceName() const
{
	return resourceName;
}

EResType::Type EResTypeHelper::getTypeFromExtension(std::string extension)
{
	boost::to_upper(extension);

	static const std::map<std::string, EResType::Type> stringToRes =
	        boost::assign::map_list_of
	        (".TXT",   EResType::TEXT)
	        (".JSON",  EResType::TEXT)
	        (".DEF",   EResType::ANIMATION)
	        (".MSK",   EResType::MASK)
	        (".MSG",   EResType::MASK)
	        (".H3C",   EResType::CAMPAIGN)
	        (".H3M",   EResType::MAP)
	        (".FNT",   EResType::BMP_FONT)
	        (".TTF",   EResType::TTF_FONT)
	        (".BMP",   EResType::IMAGE)
	        (".JPG",   EResType::IMAGE)
	        (".PCX",   EResType::IMAGE)
	        (".PNG",   EResType::IMAGE)
	        (".TGA",   EResType::IMAGE)
	        (".WAV",   EResType::SOUND)
	        (".82M",   EResType::SOUND)
	        (".SMK",   EResType::VIDEO)
	        (".BIK",   EResType::VIDEO)
	        (".MJPG",  EResType::VIDEO)
	        (".MPG",   EResType::VIDEO)
	        (".AVI",   EResType::VIDEO)
	        (".MP3",   EResType::MUSIC)
	        (".OGG",   EResType::MUSIC)
	        (".LOD",   EResType::ARCHIVE_LOD)
	        (".PAC",   EResType::ARCHIVE_LOD)
	        (".VID",   EResType::ARCHIVE_VID)
	        (".SND",   EResType::ARCHIVE_SND)
	        (".PAL",   EResType::PALETTE)
	        (".VCGM1", EResType::CLIENT_SAVEGAME)
	        (".VSGM1", EResType::SERVER_SAVEGAME)
	        (".ERM",   EResType::ERM)
	        (".ERT",   EResType::ERT)
	        (".ERS",   EResType::ERS);

	auto iter = stringToRes.find(extension);
	if (iter == stringToRes.end())
		return EResType::OTHER;
	return iter->second;
}

std::string EResTypeHelper::getEResTypeAsString(EResType::Type type)
{
#define MAP_ENUM(value) (EResType::value, #value)

	static const std::map<EResType::Type, std::string> stringToRes = boost::assign::map_list_of
		MAP_ENUM(TEXT)
		MAP_ENUM(ANIMATION)
		MAP_ENUM(MASK)
		MAP_ENUM(CAMPAIGN)
		MAP_ENUM(MAP)
		MAP_ENUM(BMP_FONT)
		MAP_ENUM(TTF_FONT)
		MAP_ENUM(IMAGE)
		MAP_ENUM(VIDEO)
		MAP_ENUM(SOUND)
		MAP_ENUM(MUSIC)
		MAP_ENUM(ARCHIVE_LOD)
		MAP_ENUM(ARCHIVE_SND)
		MAP_ENUM(ARCHIVE_VID)
		MAP_ENUM(PALETTE)
		MAP_ENUM(CLIENT_SAVEGAME)
		MAP_ENUM(SERVER_SAVEGAME)
		MAP_ENUM(DIRECTORY)
		MAP_ENUM(ERM)
		MAP_ENUM(ERT)
		MAP_ENUM(ERS)
		MAP_ENUM(OTHER);

#undef MAP_ENUM

	auto iter = stringToRes.find(type);
	assert(iter != stringToRes.end());

	return iter->second;
}

void CResourceHandler::initialize()
{
	//recurse only into specific directories
	auto recurseInDir = [](std::string URI, int depth)
	{
		auto resources = initialLoader->getResourcesWithName(ResourceID(URI, EResType::DIRECTORY));
		for(const ResourceLocator & entry : resources)
		{
			std::string filename = entry.getLoader()->getOrigin() + '/' + entry.getResourceName();
			if (!filename.empty())
			{
				shared_ptr<ISimpleResourceLoader> dir(new CFilesystemLoader(filename, depth, true));
				initialLoader->addLoader(URI + '/', dir, false);
			}
		}
	};

	//temporary filesystem that will be used to initialize main one.
	//used to solve several case-sensivity issues like Mp3 vs MP3
	initialLoader = new CResourceLoader;
	resourceLoader = new CResourceLoader;

	for (auto path : VCMIDirs::get().dataPaths())
	{
		shared_ptr<ISimpleResourceLoader> loader(new CFilesystemLoader(path, 0, true));

		initialLoader->addLoader("GLOBAL/", loader, false);
		initialLoader->addLoader("ALL/", loader, false);
	}

	{
		shared_ptr<ISimpleResourceLoader> loader(new CFilesystemLoader(VCMIDirs::get().userDataPath(), 0, true));

		initialLoader->addLoader("LOCAL/", loader, false);

		if (!vstd::contains(VCMIDirs::get().dataPaths(), VCMIDirs::get().userDataPath()))
			initialLoader->addLoader("ALL/", loader, false);
	}

	recurseInDir("ALL/CONFIG", 0);// look for configs
	recurseInDir("ALL/DATA", 0); // look for archives
	recurseInDir("ALL/MODS", 2); // look for mods. Depth 2 is required for now but won't cause spped issues if no mods present
}

void CResourceHandler::loadDirectory(const std::string &prefix, const std::string &mountPoint, const JsonNode & config)
{
	std::string URI = prefix + config["path"].String();
	bool writeable = config["writeable"].Bool();
	int depth = 16;
	if (!config["depth"].isNull())
		depth = config["depth"].Float();

	auto resources = initialLoader->getResourcesWithName(ResourceID(URI, EResType::DIRECTORY));

	for(const ResourceLocator & entry : resources)
	{
		std::string filename = entry.getLoader()->getOrigin() + '/' + entry.getResourceName();
		resourceLoader->addLoader(mountPoint,
		    shared_ptr<ISimpleResourceLoader>(new CFilesystemLoader(filename, depth)), writeable);
	}
}

void CResourceHandler::loadArchive(const std::string &prefix, const std::string &mountPoint, const JsonNode & config, EResType::Type archiveType)
{
	std::string URI = prefix + config["path"].String();
	std::string filename = initialLoader->getResourceName(ResourceID(URI, archiveType));
	if (!filename.empty())
		resourceLoader->addLoader(mountPoint,
		    shared_ptr<ISimpleResourceLoader>(new CLodArchiveLoader(filename)), false);
}

void CResourceHandler::loadJsonMap(const std::string &prefix, const std::string &mountPoint, const JsonNode & config)
{
	std::string URI = prefix + config["path"].String();
	std::string filename = initialLoader->getResourceName(ResourceID(URI, EResType::TEXT));
	if (!filename.empty())
	{
		auto configData = initialLoader->loadData(ResourceID(URI, EResType::TEXT));

		const JsonNode config((char*)configData.first.get(), configData.second);

		resourceLoader->addLoader(mountPoint,
		    shared_ptr<ISimpleResourceLoader>(new CMappedFileLoader(config)), false);
	}
}


void CResourceHandler::loadFileSystem(const std::string & prefix, const std::string &fsConfigURI)
{
	auto fsConfigData = initialLoader->loadData(ResourceID(fsConfigURI, EResType::TEXT));

	const JsonNode fsConfig((char*)fsConfigData.first.get(), fsConfigData.second);

	loadFileSystem(prefix, fsConfig["filesystem"]);
}

void CResourceHandler::loadFileSystem(const std::string & prefix, const JsonNode &fsConfig)
{
	for(auto & mountPoint : fsConfig.Struct())
	{
		for(auto & entry : mountPoint.second.Vector())
		{
			CStopWatch timer;
            logGlobal->debugStream() << "\t\tLoading resource at " << prefix + entry["path"].String();

			if (entry["type"].String() == "map")
				loadJsonMap(prefix, mountPoint.first, entry);
			if (entry["type"].String() == "dir")
				loadDirectory(prefix, mountPoint.first, entry);
			if (entry["type"].String() == "lod")
				loadArchive(prefix, mountPoint.first, entry, EResType::ARCHIVE_LOD);
			if (entry["type"].String() == "snd")
				loadArchive(prefix, mountPoint.first, entry, EResType::ARCHIVE_SND);
			if (entry["type"].String() == "vid")
				loadArchive(prefix, mountPoint.first, entry, EResType::ARCHIVE_VID);

            logGlobal->debugStream() << "Resource loaded in " << timer.getDiff() << " ms.";
		}
	}
}

std::vector<std::string> CResourceHandler::getAvailableMods()
{
	auto iterator = initialLoader->getIterator([](const ResourceID & ident) ->  bool
	{
		std::string name = ident.getName();

		return ident.getType() == EResType::DIRECTORY
		    && std::count(name.begin(), name.end(), '/') == 2
		    && boost::algorithm::starts_with(name, "ALL/MODS/");
	});

	//storage for found mods
	std::vector<std::string> foundMods;
	while (iterator.hasNext())
	{
		std::string name = iterator->getName();

		name.erase(0, name.find_last_of('/') + 1);        //Remove path prefix

		if (name == "WOG") // check if wog is actually present. Hack-ish but better than crash
		{
			if (!initialLoader->existsResource(ResourceID("ALL/DATA/ZVS", EResType::DIRECTORY)) &&
			    !initialLoader->existsResource(ResourceID("ALL/MODS/WOG/DATA/ZVS", EResType::DIRECTORY)))
			{
				++iterator;
				continue;
			}
		}

		if (!name.empty()) // this is also triggered for "ALL/MODS/" entry
			foundMods.push_back(name);

		++iterator;
	}
	return foundMods;
}

void CResourceHandler::setActiveMods(std::vector<std::string> enabledMods)
{
	// default FS config for mods: directory "Content" that acts as H3 root directory
	JsonNode defaultFS;

	defaultFS[""].Vector().resize(1);
	defaultFS[""].Vector()[0]["type"].String() = "dir";
	defaultFS[""].Vector()[0]["path"].String() = "/Content";

	for(std::string & modName : enabledMods)
	{
		ResourceID modConfFile("all/mods/" + modName + "/mod", EResType::TEXT);
		auto fsConfigData = initialLoader->loadData(modConfFile);
		const JsonNode fsConfig((char*)fsConfigData.first.get(), fsConfigData.second);

		if (!fsConfig["filesystem"].isNull())
			loadFileSystem("all/mods/" + modName, fsConfig["filesystem"]);
		else
			loadFileSystem("all/mods/" + modName, defaultFS);
	}
}
