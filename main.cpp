#include <fnmatch.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define printe(...)                   \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

namespace fs = std::filesystem;

struct FileDeleter {
  void operator()(FILE* file) const {
    if (file) {
      fclose(file);
    }
  }
};

typedef std::unique_ptr<FILE, FileDeleter> FILE_ptr;

class Opt {
 public:
  std::vector<std::string> paths;
  std::vector<std::string> extensions;
  std::vector<std::string> ignore_patterns;
  bool include_hidden = false;
  bool ignore_gitignore = false;
  bool claude_xml = false;
  std::string output_file;

  int init(int argc, char** argv) { return parse(argc, argv); }

 private:
  int parse(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, "e:o:ciH")) != -1) {
      switch (opt) {
        case 'e':
          extensions.push_back(optarg);
          break;
        case 'i':
          ignore_gitignore = true;
          break;
        case 'o':
          output_file = optarg;
          break;
        case 'c':
          claude_xml = true;
          break;
        case 'H':
          include_hidden = true;
          break;
        default:
          fprintf(
              stderr,
              "Usage: %s [-e extension] [-i ignore_pattern] [-o output_file] "
              "[-c] [-H] [paths...]\n",
              argv[0]);
          return 1;
      }
    }

    for (int i = optind; i < argc; i++) {
      paths.push_back(argv[i]);
    }

    if (paths.empty()) {
      paths.push_back(".");
    }

    return 0;
  }
};

static bool should_ignore(const std::string& path,
                          const std::vector<std::string>& gitignore_rules) {
  for (const auto& rule : gitignore_rules) {
    if (fnmatch(rule.c_str(), fs::path(path).filename().c_str(), 0) == 0) {
      return true;
    }
    if (fs::is_directory(path) &&
        fnmatch((fs::path(path).filename().string() + "/").c_str(),
                rule.c_str(), 0) == 0) {
      return true;
    }
  }
  return false;
}

static ssize_t getdelim(std::string& lineptr, int delimiter, FILE* fp) {
  ssize_t cur_len = 0;

  if (fp == nullptr)
    return -1;
  if (ferror(fp))
    return -1;

  lineptr.clear();
  std::vector<char> buffer(120);

  while (true) {
    if (fgets(buffer.data(), buffer.size(), fp) == nullptr) {
      if (feof(fp))
        break;
      return -1;
    }

    char* pos = strchr(buffer.data(), delimiter);
    if (pos != nullptr) {
      *pos = delimiter;
      lineptr.append(buffer.data(), pos - buffer.data() + 1);
      cur_len += pos - buffer.data() + 1;
      break;
    } else {
      lineptr.append(buffer.data());
      cur_len += buffer.size() - 1;
    }
  }

  return cur_len > 0 ? cur_len : -1;
}

static ssize_t getline(std::string& lineptr, FILE* stream) {
  return getdelim(lineptr, '\n', stream);
}

static std::vector<std::string> read_gitignore(const std::string& path) {
  std::vector<std::string> rules;
  std::string gitignore_path = path + "/.gitignore";
  FILE_ptr file(fopen(gitignore_path.c_str(), "r"));
  if (file) {
    std::string line;
    while (getline(line, file.get()) != -1) {
      line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
      line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
      if (line.size() > 0 && line[0] != '#') {
        rules.push_back(line);
      }
    }
  }
  return rules;
}

static void print_path(FILE* writer,
                       const std::string& path,
                       const std::string& content,
                       bool xml) {
  static int global_index = 1;
  if (xml) {
    fprintf(writer, "<document index=\"%d\">\n", global_index);
    fprintf(writer, "<source>%s</source>\n", path.c_str());
    fprintf(writer, "<document_content>\n%s\n</document_content>\n",
            content.c_str());
    fprintf(writer, "</document>\n");
    global_index++;
  } else {
    fprintf(writer, "%s\n---\n%s\n---\n", path.c_str(), content.c_str());
  }
}

static std::string read_file_content(const std::string& path) {
  FILE_ptr file(fopen(path.c_str(), "r"));
  if (file) {
    fseek(file.get(), 0, SEEK_END);
    size_t size = ftell(file.get());
    rewind(file.get());
    std::string content(size, ' ');
    fread(&content[0], size, 1, file.get());
    return content;
  } else {
    printe("Warning: Skipping file %s due to error opening file\n",
           path.c_str());
    return "";
  }
}

static bool should_ignore_file(const std::string& filename,
                               const std::vector<std::string>& ignore_patterns,
                               const std::vector<std::string>& extensions,
                               bool include_hidden) {
  if (!include_hidden && filename[0] == '.') {
    return true;
  }

  for (const auto& pattern : ignore_patterns) {
    if (fnmatch(pattern.c_str(), filename.c_str(), 0) == 0) {
      return true;
    }
  }

  if (!extensions.empty()) {
    for (const auto& ext : extensions) {
      if (filename.size() >= ext.size() &&
          filename.compare(filename.size() - ext.size(), ext.size(), ext) ==
              0) {
        return false;
      }
    }
    return true;
  }

  return false;
}

static void process_file(const std::string& path,
                         FILE* writer,
                         bool claude_xml) {
  std::string content = read_file_content(path);
  if (!content.empty()) {
    print_path(writer, path, content, claude_xml);
  }
}

static void process_directory(const std::string& path,
                              const std::vector<std::string>& extensions,
                              bool include_hidden,
                              bool ignore_gitignore,
                              std::vector<std::string>& gitignore_rules,
                              const std::vector<std::string>& ignore_patterns,
                              FILE* writer,
                              bool claude_xml) {
  for (const auto& entry : fs::recursive_directory_iterator(path)) {
    if (fs::is_directory(entry.status()))
      continue;

    std::string file_path = entry.path().string();
    std::string filename = entry.path().filename().string();
    if (should_ignore_file(filename, ignore_patterns, extensions,
                           include_hidden))
      continue;

    if (!ignore_gitignore && should_ignore(file_path, gitignore_rules))
      continue;

    process_file(file_path, writer, claude_xml);
  }
}

static void process_path(const std::string& path,
                         const std::vector<std::string>& extensions,
                         bool include_hidden,
                         bool ignore_gitignore,
                         std::vector<std::string>& gitignore_rules,
                         const std::vector<std::string>& ignore_patterns,
                         FILE* writer,
                         bool claude_xml) {
  if (fs::is_regular_file(path)) {
    process_file(path, writer, claude_xml);
  } else if (fs::is_directory(path)) {
    process_directory(path, extensions, include_hidden, ignore_gitignore,
                      gitignore_rules, ignore_patterns, writer, claude_xml);
  }
}

int main(int argc, char** argv) {
  Opt opt;
  if (opt.init(argc, argv)) {
    return 1;
  }

  std::vector<std::string> gitignore_rules;
  FILE* writer = stdout;
  FILE_ptr file_out;
  if (!opt.output_file.empty()) {
    file_out.reset(fopen(opt.output_file.c_str(), "w"));
    writer = file_out.get();
  }

  for (const auto& path : opt.paths) {
    if (!fs::exists(path)) {
      printe("Path does not exist: %s\n", path.c_str());
      return 1;
    }
    if (!opt.ignore_gitignore) {
      auto rules = read_gitignore(fs::path(path).parent_path().string());
      gitignore_rules.insert(gitignore_rules.end(), rules.begin(), rules.end());
    }
    if (opt.claude_xml && path == opt.paths[0]) {
      fprintf(writer, "<documents>\n");
    }
    process_path(path, opt.extensions, opt.include_hidden, opt.ignore_gitignore,
                 gitignore_rules, opt.ignore_patterns, writer, opt.claude_xml);
  }
  if (opt.claude_xml) {
    fprintf(writer, "</documents>\n");
  }

  return 0;
}
