#ifndef ARG_PARSER_H_
#define ARG_PARSER_H_

#include <map>
#include <string>

class ArgParser {
  public:
	ArgParser(int argc, char* argv[]);
	bool valid_args() { return valid_args_; }
	std::string get_argument(std::string option);

  private:
	bool valid_args_;
	std::map<std::string, std::string> option_to_arg_;
};

#endif // ARG_PARSER_H_
