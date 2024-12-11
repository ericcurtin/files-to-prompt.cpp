#include <fnmatch.h>
#include <unistd.h>
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

static std::vector<std::string> read_gitignore(const std::string& path) {
  std::vector<std::string> rules;
  std::string gitignore_path = path + "/.gitignore";
  FILE_ptr file(fopen(gitignore_path.c_str(), "r"));
  if (file) {
    char* line = nullptr;
    size_t len = 0;
    while (getline(&line, &len, file.get()) != -1) {
      line[strcspn(line, "\r\n")] =
          0;  // Remove any carriage returns and newlines
      if (strlen(line) > 0 && line[0] != '#') {
        rules.push_back(line);
      }
    }
    free(line);
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
  // Command-line options
  std::vector<std::string> paths;
  std::vector<std::string> extensions;
  std::vector<std::string> ignore_patterns;
  bool include_hidden = false;
  bool ignore_gitignore = false;
  bool claude_xml = false;
  std::string output_file;

  int opt;
  while ((opt = getopt(argc, argv, "e:i:o:cH")) != -1) {
    switch (opt) {
      case 'e':
        extensions.push_back(optarg);
        break;
      case 'i':
        ignore_patterns.push_back(optarg);
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
        fprintf(stderr,
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

  std::vector<std::string> gitignore_rules;
  FILE* writer = stdout;
  FILE_ptr file_out;
  if (!output_file.empty()) {
    file_out.reset(fopen(output_file.c_str(), "w"));
    writer = file_out.get();
  }

  for (const auto& path : paths) {
    if (!fs::exists(path)) {
      printe("Path does not exist: %s\n", path.c_str());
      return 1;
    }
    if (!ignore_gitignore) {
      auto rules = read_gitignore(fs::path(path).parent_path().string());
      gitignore_rules.insert(gitignore_rules.end(), rules.begin(), rules.end());
    }
    if (claude_xml && path == paths[0]) {
      fprintf(writer, "<documents>\n");
    }
    process_path(path, extensions, include_hidden, ignore_gitignore,
                 gitignore_rules, ignore_patterns, writer, claude_xml);
  }
  if (claude_xml) {
    fprintf(writer, "</documents>\n");
  }

  return 0;
}
