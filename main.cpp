#include <fnmatch.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int global_index = 1;

bool should_ignore(const std::string& path,
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

std::vector<std::string> read_gitignore(const std::string& path) {
  std::vector<std::string> rules;
  std::string gitignore_path = path + "/.gitignore";
  FILE* file = fopen(gitignore_path.c_str(), "r");
  if (file) {
    char* line = nullptr;
    size_t len = 0;
    while (getline(&line, &len, file) != -1) {
      line[strcspn(line, "\r\n")] =
          0;  // Remove any carriage returns and newlines
      if (strlen(line) > 0 && line[0] != '#') {
        rules.push_back(line);
      }
    }
    free(line);
    fclose(file);
  }
  return rules;
}

void print_path(FILE* writer,
                const std::string& path,
                const std::string& content,
                bool xml) {
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

void process_path(const std::string& path,
                  const std::vector<std::string>& extensions,
                  bool include_hidden,
                  bool ignore_gitignore,
                  std::vector<std::string>& gitignore_rules,
                  const std::vector<std::string>& ignore_patterns,
                  FILE* writer,
                  bool claude_xml) {
  if (fs::is_regular_file(path)) {
    FILE* file = fopen(path.c_str(), "r");
    if (file) {
      fseek(file, 0, SEEK_END);
      size_t size = ftell(file);
      rewind(file);
      char* content = (char*)malloc(size + 1);
      fread(content, size, 1, file);
      content[size] = '\0';
      print_path(writer, path, content, claude_xml);
      free(content);
      fclose(file);
    } else {
      fprintf(stderr, "Warning: Skipping file %s due to error opening file\n",
              path.c_str());
    }
  } else if (fs::is_directory(path)) {
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      if (fs::is_directory(entry.status()))
        continue;

      std::string file_path = entry.path().string();
      std::string filename = entry.path().filename().string();

      if (!include_hidden && filename[0] == '.')
        continue;
      if (!ignore_gitignore && should_ignore(file_path, gitignore_rules))
        continue;
      bool ignore = false;
      for (const auto& pattern : ignore_patterns) {
        if (fnmatch(pattern.c_str(), filename.c_str(), 0) == 0) {
          ignore = true;
          break;
        }
      }
      if (ignore)
        continue;
      if (!extensions.empty()) {
        bool match = false;
        for (const auto& ext : extensions) {
          if (filename.size() >= ext.size() &&
              filename.compare(filename.size() - ext.size(), ext.size(), ext) ==
                  0) {
            match = true;
            break;
          }
        }
        if (!match)
          continue;
      }
      FILE* file = fopen(file_path.c_str(), "r");
      if (file) {
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        rewind(file);
        char* content = (char*)malloc(size + 1);
        fread(content, size, 1, file);
        content[size] = '\0';
        print_path(writer, file_path, content, claude_xml);
        free(content);
        fclose(file);
      } else {
        fprintf(stderr, "Warning: Skipping file %s due to error opening file\n",
                file_path.c_str());
      }
    }
  }
}

int main(int argc, char** argv) {
  // Parsing command-line arguments would be needed here, similar to click in
  // Python This is a simplified version for the sake of conversion
  std::vector<std::string> paths = {"."};                // Example paths
  std::vector<std::string> extensions = {".cpp", ".h"};  // Example extensions
  bool include_hidden = false;
  bool ignore_gitignore = false;
  std::vector<std::string> ignore_patterns;
  bool claude_xml = false;
  std::string output_file;

  std::vector<std::string> gitignore_rules;
  FILE* writer = stdout;
  FILE* file_out = nullptr;

  if (!output_file.empty()) {
    file_out = fopen(output_file.c_str(), "w");
    writer = file_out;
  }

  for (const auto& path : paths) {
    if (!fs::exists(path)) {
      fprintf(stderr, "Path does not exist: %s\n", path.c_str());
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

  if (file_out) {
    fclose(file_out);
  }

  return 0;
}
