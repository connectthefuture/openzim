/*
 * Copyright 2013-2016 Emmanuel Engelhart <kelson@kiwix.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU  General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <ctime>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include <iomanip>
#include <fstream>
#include <sstream>
#include <queue>
#include <map>
#include <cstdio>
#include <magic.h>

#include <zim/writer/zimcreator.h>
#include <zim/blob.h>


#include "tools.h"
#include "article.h"

#define MAX_QUEUE_SIZE 100

std::string language;
std::string creator;
std::string publisher;
std::string title;
std::string description;
std::string welcome;
std::string favicon; 
std::string directoryPath;
std::string redirectsPath;
std::string zimPath;
zim::writer::ZimCreator zimCreator;
pthread_t directoryVisitor;
pthread_mutex_t filenameQueueMutex;
std::queue<std::string> filenameQueue;
std::queue<std::string> metadataQueue;
std::queue<std::string> redirectsQueue;

bool isDirectoryVisitorRunningFlag = false;
pthread_mutex_t directoryVisitorRunningMutex;
bool verboseFlag = false;
pthread_mutex_t verboseMutex;
bool inflateHtmlFlag = false;
bool uniqueNamespace = false;

magic_t magic;
std::map<std::string, unsigned int> counters;
char *data = NULL;
unsigned int dataSize = 0;


void directoryVisitorRunning(bool value) {
  pthread_mutex_lock(&directoryVisitorRunningMutex);
  isDirectoryVisitorRunningFlag = value;
  pthread_mutex_unlock(&directoryVisitorRunningMutex); 
}

bool isDirectoryVisitorRunning() {
  pthread_mutex_lock(&directoryVisitorRunningMutex);
  bool retVal = isDirectoryVisitorRunningFlag;
  pthread_mutex_unlock(&directoryVisitorRunningMutex); 
  return retVal;
}

bool isVerbose() {
  pthread_mutex_lock(&verboseMutex);
  bool retVal = verboseFlag;
  pthread_mutex_unlock(&verboseMutex); 
  return retVal;
}

bool isFilenameQueueEmpty() {
  pthread_mutex_lock(&filenameQueueMutex);
  bool retVal = filenameQueue.empty();
  pthread_mutex_unlock(&filenameQueueMutex);
  return retVal;
}

void pushToFilenameQueue(const std::string &filename) {
  unsigned int wait = 0;
  unsigned int queueSize = 0;

  do {
    usleep(wait);
    pthread_mutex_lock(&filenameQueueMutex);
    unsigned queueSize = filenameQueue.size();
    pthread_mutex_unlock(&filenameQueueMutex);
    wait += 10;
  } while (queueSize > MAX_QUEUE_SIZE);

  pthread_mutex_lock(&filenameQueueMutex);
  filenameQueue.push(filename);
  pthread_mutex_unlock(&filenameQueueMutex); 
}

bool popFromFilenameQueue(std::string &filename) {
  bool retVal = false;
  unsigned int wait = 0;

  do {
    usleep(wait);
    if (!isFilenameQueueEmpty()) {
      pthread_mutex_lock(&filenameQueueMutex);
      filename = filenameQueue.front();
      filenameQueue.pop();
      pthread_mutex_unlock(&filenameQueueMutex);
      retVal = true;
      break;
    } else {
      wait += 10;
    }
  } while (isDirectoryVisitorRunning() || !isFilenameQueueEmpty());

  return retVal;
}

/* ArticleSource class */
class ArticleSource : public zim::writer::ArticleSource {
  public:
    explicit ArticleSource();
    virtual const zim::writer::Article* getNextArticle();
    virtual zim::Blob getData(const std::string& aid);
    virtual std::string getMainPage();
};

ArticleSource::ArticleSource() {
}

std::string ArticleSource::getMainPage() {
  return welcome;
}

Article *article = NULL;
const zim::writer::Article* ArticleSource::getNextArticle() {
  std::string path;

  if (article != NULL) {
    delete(article);
  }

  if (!metadataQueue.empty()) {
    path = metadataQueue.front();
    metadataQueue.pop();
    article = new MetadataArticle(path);
  } else if (!redirectsQueue.empty()) {
    std::string line = redirectsQueue.front();
    redirectsQueue.pop();
    article = new RedirectArticle(line);
  } else if (popFromFilenameQueue(path)) {
    do {
      article = new Article(path);
    } while (article && article->isInvalid() && popFromFilenameQueue(path));
  } else {
    article = NULL;
  }

  /* Count mimetypes */
  if (article != NULL && !article->isRedirect()) {

    if (isVerbose())
      std::cout << "Creating entry for " << article->getAid() << std::endl;

    std::string mimeType = article->getMimeType();
    if (counters.find(mimeType) == counters.end()) {
      counters[mimeType] = 1;
    } else {
      counters[mimeType]++;
    }
  }

  return article;
}

zim::Blob ArticleSource::getData(const std::string& aid) {

  if (isVerbose())
    std::cout << "Packing data for " << aid << std::endl;

  if (data != NULL) {
    delete(data);
    data = NULL;
  }

  if (aid.substr(0, 3) == "/M/") {
    std::string value; 

    if ( aid == "/M/Language") {
      value = language;
    } else if (aid == "/M/Creator") {
      value = creator;
    } else if (aid == "/M/Publisher") {
      value = publisher;
    } else if (aid == "/M/Title") {
      value = title;
    } else if (aid == "/M/Description") {
      value = description;
    } else if ( aid == "/M/Date") {
      time_t t = time(0);
      struct tm * now = localtime( & t );
      std::stringstream stream;
      stream << (now->tm_year + 1900) << '-' 
	     << std::setw(2) << std::setfill('0') << (now->tm_mon + 1) << '-'
	     << std::setw(2) << std::setfill('0') << now->tm_mday;
      value = stream.str();
    } else if ( aid == "/M/Counter") {
      std::stringstream stream;
      for (std::map<std::string, unsigned int>::iterator it = counters.begin(); it != counters.end(); ++it) {
	stream << it->first << "=" << it->second << ";";
      }
      value = stream.str();
    }

    dataSize = value.length();
    data = new char[dataSize];
    memcpy(data, value.c_str(), dataSize);
  } else {
    std::string aidPath = directoryPath + "/" + aid;
    
    if (getMimeTypeForFile(aid).find("text/html") == 0) {
      std::string html = getFileContent(aidPath);
      
      /* Rewrite links (src|href|...) attributes */
      GumboOutput* output = gumbo_parse(html.c_str());
      GumboNode* root = output->root;

      std::map<std::string, bool> links;
      getLinks(root, links);
      std::map<std::string, bool>::iterator it;
      std::string aidDirectory = removeLastPathElement(aid, false, false);
      
      /* If a link appearch to be duplicated in the HTML, it will
	 occurs only one time in the links variable */
      for(it = links.begin(); it != links.end(); it++) {
	if (!it->first.empty() && it->first[0] != '#' && it->first[0] != '?' && it->first.substr(0, 5) != "data:") {
	  replaceStringInPlace(html, "\"" + it->first + "\"", "\"" + computeNewUrl(aid, it->first) + "\"");
	}
      }
      gumbo_destroy_output(&kGumboDefaultOptions, output);

      dataSize = html.length();
      data = new char[dataSize];
      memcpy(data, html.c_str(), dataSize);
    } else if (getMimeTypeForFile(aid).find("text/css") == 0) {
      std::string css = getFileContent(aidPath);

      /* Rewrite url() values in the CSS */
      size_t startPos = 0;
      size_t endPos = 0;
      std::string url;

      while ((startPos = css.find("url(", endPos)) && startPos != std::string::npos) {

	/* URL delimiters */
	endPos = css.find(")", startPos);
	startPos = startPos + (css[startPos+4] == '\'' || css[startPos+4] == '"' ? 5 : 4);
	endPos = endPos - (css[endPos-1] == '\'' || css[endPos-1] == '"' ? 1 : 0);
	url = css.substr(startPos, endPos - startPos);
	std::string startDelimiter = css.substr(startPos-1, 1);
	std::string endDelimiter = css.substr(endPos, 1);

	if (url.substr(0, 5) != "data:") {
	  /* Deal with URL with arguments (using '? ') */
	  std::string path = url;
	  size_t markPos = url.find("?");
	  if (markPos != std::string::npos) {
	    path = url.substr(0, markPos);
	  }

	  /* Embeded fonts need to be inline because Kiwix is
	     otherwise not able to load same because of the
	     same-origin security */
	  std::string mimeType = getMimeTypeForFile(path);
	  if (mimeType == "application/font-ttf" || 
	      mimeType == "application/font-woff" || 
	      mimeType == "application/vnd.ms-opentype" ||
	      mimeType == "application/vnd.ms-fontobject") {

	    try {
	      std::string fontContent = getFileContent(directoryPath + "/" + computeAbsolutePath(aid, path));
	      replaceStringInPlaceOnce(css, 
				       startDelimiter + url + endDelimiter, 
				       startDelimiter + "data:" + mimeType + ";base64," + 
				       base64_encode(reinterpret_cast<const unsigned char*>(fontContent.c_str()), fontContent.length()) +
				       endDelimiter
				       );
	    } catch (...) {
	    }
	  } else {

	    /* Deal with URL with arguments (using '? ') */
	    if (markPos != std::string::npos) {
	      endDelimiter = url.substr(markPos, 1);
	    }

	    replaceStringInPlaceOnce(css,
				     startDelimiter + url + endDelimiter,
				     startDelimiter + computeNewUrl(aid, path) + endDelimiter);
	  }
	}
      }

      dataSize = css.length();
      data = new char[dataSize];
      memcpy(data, css.c_str(), dataSize);
    } else {
      dataSize = getFileSize(aidPath);
      data = new char[dataSize];
      memcpy(data, getFileContent(aidPath).c_str(), dataSize);
    }
  }

  return zim::Blob(data, dataSize);
}

/* Non ZIM related code */
void usage() {
  std::cout << "Usage: zimwriterfs [mandatory arguments] [optional arguments] HTML_DIRECTORY ZIM_FILE" << std::endl;
  std::cout << std::endl;

  std::cout << "Purpose:" << std::endl;
  std::cout << "\tPacking all files (HTML/JS/CSS/JPEG/WEBM/...) belonging to a directory in a ZIM file." << std::endl;
  std::cout << std::endl;

  std::cout << "Mandatory arguments:" << std::endl;
  std::cout << "\t-w, --welcome\t\tpath of default/main HTML page. The path must be relative to HTML_DIRECTORY." << std::endl;
  std::cout << "\t-f, --favicon\t\tpath of ZIM file favicon. The path must be relative to HTML_DIRECTORY and the image a 48x48 PNG." << std::endl;
  std::cout << "\t-l, --language\t\tlanguage code of the content in ISO639-3" << std::endl;
  std::cout << "\t-t, --title\t\ttitle of the ZIM file" << std::endl;
  std::cout << "\t-d, --description\tshort description of the content" << std::endl;
  std::cout << "\t-c, --creator\t\tcreator(s) of the content" << std::endl;
  std::cout << "\t-p, --publisher\t\tcreator of the ZIM file itself" << std::endl;
  std::cout << std::endl;
  std::cout << "\tHTML_DIRECTORY\t\tis the path of the directory containing the HTML pages you want to put in the ZIM file," << std::endl;
  std::cout << "\tZIM_FILE\t\tis the path of the ZIM file you want to obtain." << std::endl;
  std::cout << std::endl;

  std::cout << "Optional arguments:" << std::endl;
  std::cout << "\t-v, --verbose\t\tprint processing details on STDOUT" << std::endl;
  std::cout << "\t-h, --help\t\tprint this help" << std::endl;
  std::cout << "\t-m, --minChunkSize\tnumber of bytes per ZIM cluster (defaul: 2048)" << std::endl;
  std::cout << "\t-x, --inflateHtml\ttry to inflate HTML files before packing (*.html, *.htm, ...)" << std::endl;
  std::cout << "\t-u, --uniqueNamespace\tput everything in the same namespace 'A'. Might be necessary to avoid problems with dynamic/javascript data loading." << std::endl;
  std::cout << "\t-r, --redirects\t\tpath to the CSV file with the list of redirects (url, title, target_url tab separated)." << std::endl;
  std::cout << std::endl;
 
   std::cout << "Example:" << std::endl;
  std::cout << "\tzimwriterfs --welcome=index.html --favicon=m/favicon.png --language=fra --title=foobar --description=mydescription \\\n\t\t\
--creator=Wikipedia --publisher=Kiwix ./my_project_html_directory my_project.zim" << std::endl;
  std::cout << std::endl;

  std::cout << "Documentation:" << std::endl;
  std::cout << "\tzimwriterfs source code: http://www.openzim.org/wiki/Git" << std::endl;
  std::cout << "\tZIM format: http://www.openzim.org/" << std::endl;
  std::cout << std::endl;
}

void *visitDirectory(const std::string &path) {

  if (isVerbose())
    std::cout << "Visiting directory " << path << std::endl;

  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
  DIR *directory;

  /* Open directory */
  directory = opendir(path.c_str());
  if (directory == NULL) {
    std::cerr << "zimwriterfs: unable to open directory " << path << std::endl;
    exit(1);
  }

  /* Read directory content */
  struct dirent *entry;
  while (entry = readdir(directory)) {
    std::string entryName = entry->d_name;

    /* Ignore this system navigation virtual directories */
    if (entryName != "." && entryName != "..") {
      std::string fullEntryName = path + '/' + entryName;

      switch (entry->d_type) {
      case DT_REG:
	pushToFilenameQueue(fullEntryName);
	break;
      case DT_DIR:
	visitDirectory(fullEntryName);
	break;
      case DT_BLK:
	std::cerr << "Unable to deal with " << fullEntryName << " (this is a block device)" << std::endl;
	break;
      case DT_CHR:
	std::cerr << "Unable to deal with " << fullEntryName << " (this is a character device)" << std::endl;
	break;
      case DT_FIFO:
	std::cerr << "Unable to deal with " << fullEntryName << " (this is a named pipe)" << std::endl;
	break;
      case DT_LNK:
	pushToFilenameQueue(fullEntryName);
	break;
      case DT_SOCK:
	std::cerr << "Unable to deal with " << fullEntryName << " (this is a UNIX domain socket)" << std::endl;
	break;
      case DT_UNKNOWN:
	struct stat s;
	if (stat(fullEntryName.c_str(), &s) == 0) {
	  if (S_ISREG(s.st_mode)) {
	    pushToFilenameQueue(fullEntryName);
	  } else if (S_ISDIR(s.st_mode)) {
	    visitDirectory(fullEntryName);
          } else {
	    std::cerr << "Unable to deal with " << fullEntryName << " (no clue what kind of file it is - from stat())" << std::endl;
	  }
	} else {
	  std::cerr << "Unable to stat " << fullEntryName << std::endl;
	}
	break;
      default:
	std::cerr << "Unable to deal with " << fullEntryName << " (no clue what kind of file it is)" << std::endl;
	break;
      }
    }
  }
  
  closedir(directory);
}

void *visitDirectoryPath(void *path) {
  visitDirectory(directoryPath);

  if (isVerbose())
    std::cout << "Quitting visitor" << std::endl;

  directoryVisitorRunning(false); 
  pthread_exit(NULL);
}

int main(int argc, char** argv) {
  ArticleSource source;
  int minChunkSize = 2048;
  

  /* Argument parsing */
  static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"verbose", no_argument, 0, 'v'},
    {"welcome", required_argument, 0, 'w'},
    {"minchunksize", required_argument, 0, 'm'},
    {"redirects", required_argument, 0, 'r'},
    {"inflateHtml", no_argument, 0, 'x'},
    {"uniqueNamespace", no_argument, 0, 'u'},
    {"favicon", required_argument, 0, 'f'},
    {"language", required_argument, 0, 'l'},
    {"title", required_argument, 0, 't'},
    {"description", required_argument, 0, 'd'},
    {"creator", required_argument, 0, 'c'},
    {"publisher", required_argument, 0, 'p'},
    {0, 0, 0, 0}
  };
  int option_index = 0;
  int c;

  do { 
    c = getopt_long(argc, argv, "hvxuw:m:f:t:d:c:l:p:r:", long_options, &option_index);
    
    if (c != -1) {
      switch (c) {
      case 'h':
	usage();
	exit(0);	
	break;
      case 'v':
	verboseFlag = true;
	break;
      case 'x':
	inflateHtmlFlag = true;
	break;
      case 'c':
	creator = optarg;
	break;
      case 'd':
	description = optarg;
	break;
      case 'f':
	favicon = optarg;
	break;
      case 'l':
	language = optarg;
	break;
      case 'm':
	minChunkSize = atoi(optarg);
	break;
      case 'p':
	publisher = optarg;
	break;
      case 'r':
	redirectsPath = optarg;
	break;
      case 't':
	title = optarg;
	break;
      case 'u':
	uniqueNamespace = true;
	break;
      case 'w':
	welcome = optarg;
	break;
      }
    }
  } while (c != -1);

  while (optind < argc) {
    if (directoryPath.empty()) {
      directoryPath = argv[optind++];
    } else if (zimPath.empty()) {
      zimPath = argv[optind++];
    } else {
      break;
    }
  }
  
  if (directoryPath.empty() || zimPath.empty() || creator.empty() || publisher.empty() || description.empty() || language.empty() || welcome.empty() || favicon.empty()) {
    if (argc > 1)
      std::cerr << "zimwriterfs: too few arguments!" << std::endl;
    usage();
    exit(1);
  }

  /* Check arguments */
  if (directoryPath[directoryPath.length()-1] == '/') {
    directoryPath = directoryPath.substr(0, directoryPath.length()-1);
  }

  /* Prepare metadata */
  metadataQueue.push("Language");
  metadataQueue.push("Publisher");
  metadataQueue.push("Creator");
  metadataQueue.push("Title");
  metadataQueue.push("Description");
  metadataQueue.push("Date");
  metadataQueue.push("Favicon");
  metadataQueue.push("Counter");

  /* Check metadata */
  if (!fileExists(directoryPath + "/" + welcome)) {
    std::cerr << "zimwriterfs: unable to find welcome page at '" << directoryPath << "/" << welcome << "'. --welcome path/value must be relative to HTML_DIRECTORY." << std::endl;
    exit(1);
  }

  if (!fileExists(directoryPath + "/" + favicon)) {
    std::cerr << "zimwriterfs: unable to find favicon at " << directoryPath << "/" << favicon << "'. --favicon path/value must be relative to HTML_DIRECTORY." << std::endl;
    exit(1);
  }

  /* Check redirects file and read it if necessary*/
  if (!redirectsPath.empty() && !fileExists(redirectsPath)) {
    std::cerr << "zimwriterfs: unable to find redirects CSV file at '" << redirectsPath << "'. Verify --redirects path/value." << std::endl;
    exit(1);
  } else {
    if (isVerbose())
      std::cout << "Reading redirects CSV file " << redirectsPath << "..." << std::endl;

    std::ifstream in_stream;
    std::string line;

    in_stream.open(redirectsPath.c_str());
    while (std::getline(in_stream, line)) {
      redirectsQueue.push(line);
    }
    in_stream.close();
  }

  /* Init */
  magic = magic_open(MAGIC_MIME);
  magic_load(magic, NULL);
  pthread_mutex_init(&filenameQueueMutex, NULL);
  pthread_mutex_init(&directoryVisitorRunningMutex, NULL);
  pthread_mutex_init(&verboseMutex, NULL);

  /* Directory visitor */
  directoryVisitorRunning(true);
  pthread_create(&(directoryVisitor), NULL, visitDirectoryPath, (void*)NULL);
  pthread_detach(directoryVisitor);

  /* ZIM creation */
  setenv("ZIM_LZMA_LEVEL", "9e", 1);
  try {
    zimCreator.setMinChunkSize(minChunkSize);
    zimCreator.create(zimPath, source);
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  /* Destroy mutex */
  pthread_mutex_destroy(&directoryVisitorRunningMutex);
  pthread_mutex_destroy(&verboseMutex);
  pthread_mutex_destroy(&filenameQueueMutex);
}
