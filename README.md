# files-to-prompt.cpp

A rewrite of files-to-prompt in C++

## Features

- Reads `.gitignore` files and applies the rules.
- Processes files and directories recursively.
- Supports filtering by file extensions and hidden files.
- Outputs file contents in plain text or XML format.

## Usage

```sh
files-to-prompt.cpp [options]
```

### Options

- `-e`: Specify file extensions to include (e.g., `.cpp`, `.h`).
- `-H`: Include hidden files in the processing.
- `-i`: Ignore rules specified in `.gitignore` files.
- `-o`: Specify an output file to save results.
- `-c`: Output results in XML format.

## Example

To process all `.cpp` and `.h` files in the current directory, including hidden files, and output the results in XML format:

```sh
files-to-prompt.cpp -e .cpp -e .h -i -c
```

## Contributing

Contributions are welcome! Please fork the repository, make your changes, and submit a pull request.

