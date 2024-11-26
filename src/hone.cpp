#include "../include/colours.hpp"
#include "../include/CLI11.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <regex>

using json = nlohmann::json;


class AUR_Helper {
public:
    int32_t Start(const std::string &install_query, const std::string &remove_query, const std::string &search_query, bool only_name, bool is_list, bool update, bool no_syu)
    {
        // ? Restrict the use of multiple arguments
        if (Is_More_Than_One_Options(install_query, remove_query, search_query, is_list, update)) {
            std::cerr << "Error: You can only use a single option at a time!\n";
            return ERR_CODE;
        }
        // ? Checks if --name is being used when --search is not being used
        if (is_list && !search_query.empty()) {
            std::cerr << "Error: Do not use --name outside of --search!\n";
            return ERR_CODE;
        }
        // ? Checks if --no-sysupgrade is being used when --update is not being used
        if (no_syu && !update) {
            std::cerr << "Error: Do not use --no-sysupgrade outside of --update!\n";
            return ERR_CODE;
        }

        if (!remove_query.empty()) Remove_Installed_PKG(remove_query);
        else if (!install_query.empty()) Install_AUR_PKG(install_query);
        else if (!search_query.empty()) Search_PKGs(search_query, only_name);
        else if (update) Perform_Upgrades(no_syu);
        else if (is_list) Print_PKG_List();
        return SUCCESS_CODE;
    }

private:
    const int32_t SUCCESS_CODE = 0;
    const int32_t ERR_CODE = 1;
    const std::string HOME_DIR = std::getenv("HOME");
    const std::string INSTALL_PATH = HOME_DIR + "/.cache/hone/";


    bool Does_Install_Dir_Exists()
    {
        return std::filesystem::exists(INSTALL_PATH) && std::filesystem::is_directory(INSTALL_PATH);
    }


    bool Is_More_Than_One_Options(const std::string &install_query, const std::string &remove_query, const std::string &search_query, bool is_list, bool update)
    {
        int8_t option_count = 0;
        if (!install_query.empty()) option_count++;
        if (!remove_query.empty()) option_count++;
        if (!search_query.empty()) option_count++;
        if (is_list) option_count++;
        if (update) option_count++;

        return option_count > 1;
    }

    // ? Callback function to write response data from curl
    static std::size_t Write_Callback(void *contents, std::size_t size, std::size_t nmemb, std::string *userp)
    {
        userp->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }


    bool Is_PKG_Installed(const std::string &pkg_name)
    {
        const std::string command = "pacman -Q " + pkg_name + " 2>/dev/null";
        std::string result;

        auto pipe = popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << WARNING_COLOUR << "popen() failed in Is_PKG_Installed(): " << strerror(errno) << '\n';
            return false;
        }

        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) result += buffer;

        if (pclose(pipe) == -1) {
            std::cerr << WARNING_COLOUR << "pclose() failed in Is_PKG_Installed(): " << strerror(errno) << '\n';
            return false;
        }

        return !result.empty();
    }


    void Search_PKGs(const std::string &search_query, bool only_name)
    {
        CURL *curl = curl_easy_init();;
        std::string read_buffer;
        if (!curl) return;
        CURLcode res;

        std::string url = "https://aur.archlinux.org/rpc/?v=5&type=search&arg=" + search_query; 
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if (curl_easy_perform(curl) != CURLE_OK) {
            std::cerr << WARNING_COLOUR << "Failed to perform curl request.\n";
            curl_easy_cleanup(curl);
            return;
        }
        curl_easy_cleanup(curl);

        // ? Parse json response
        auto json_response = json::parse(read_buffer);
        if (!json_response["resultcount"]) {
            std::cout << "No packages found.\n";
            return;
        }

        std::regex include_pattern(".*" + std::regex_replace(search_query, std::regex(R"([.*+?^${}()|\[\]\\])"), R"(\\$&)") + ".*", std::regex_constants::icase);
        for (const auto &pkg : json_response["results"]) {
            std::string pkg_name = pkg.value("Name", "Unknown");

            if (std::regex_match(pkg_name, include_pattern)) {
                if (only_name) {
                    std::cout << pkg_name << '\n';
                    continue;
                }

                std::string pkg_desc = pkg.contains("Description") ? pkg["Description"] : "No description available";
                std::string pkg_version = pkg.contains("Version") ? pkg["Version"] : "Unknown";
                std::string installed_text = Is_PKG_Installed(pkg_name) ? "(Installed)" : "";
                std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";

                std::cout << NAME_COLOUR + pkg_name << ' ' << VERSION_COLOUR + pkg_version << ' ' << INSTALLED_COLOUR + installed_text << '\n';
                std::cout << "    " << RESET + pkg_desc << '\n';
            }
        }
    }


    std::string Get_PKG_Version(const std::string &pkg_name)
    {
        std::string read_buffer;
        CURL *curl = curl_easy_init();
        if (!curl) return "Not found";

        std::string url = "https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + pkg_name;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        if (curl_easy_perform(curl) != CURLE_OK) {
            std::cerr << "Failed to perform curl request.\n";
            curl_easy_cleanup(curl);
            return "Failed to perform curl request";
        }
        curl_easy_cleanup(curl);

        auto json_response = json::parse(read_buffer);
        if (json_response["resultcount"] > 0) return json_response["results"][0]["Version"];
        return "Not found";
    }


    std::vector<std::string> Get_PKG_List()
    {
        const std::string command = "pacman -Qm";
        std::vector<std::string> pkg_list;

        auto pipe = popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << WARNING_COLOUR << "popen() failed in Is_PKG_Installed(): " << strerror(errno) << '\n';
            return pkg_list;
        }

        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pkg_list.push_back(buffer);
        }

        if (pclose(pipe) == -1) {
            std::cerr << WARNING_COLOUR << "pclose() failed in Is_PKG_Installed(): " << strerror(errno) << '\n';
            return pkg_list;
        }
        return pkg_list;
    }


    void Print_PKG_List()
    {
        const std::vector<std::string> pkg_list = Get_PKG_List();

        if (pkg_list.empty()) {
            std::cout << "No packages installed.\n";
            return;
        }

        for (const auto &pkg : pkg_list) {
            std::istringstream iss(pkg);
            std::string pkg_version;
            std::string pkg_name;
            iss >> pkg_name >> pkg_version;

            std::cout << NAME_COLOUR << pkg_name << ' ' << VERSION_COLOUR << pkg_version << RESET << '\n';
        }
    }


    int32_t Perform_Upgrades(const bool &no_syu)
    {
        std::cout << "Performing upgrades!\n";
        if (Update_PKGs(Check_For_Updates(), no_syu)) return ERR_CODE;
        return SUCCESS_CODE;
    }


    int32_t Clone_AUR_PKG(const std::string &pkg_query)
    {
        std::cout << "Cloning package!\n";
        if (!Does_Install_Dir_Exists()) std::filesystem::create_directory(INSTALL_PATH);

        std::filesystem::current_path(INSTALL_PATH);
        std::string command = "git clone https://aur.archlinux.org/" + pkg_query + ".git " + INSTALL_PATH + pkg_query;

        if (std::system(command.c_str())) {
            std::cerr << WARNING_COLOUR << "Failed to clone AUR package!\n";
            return ERR_CODE;
        }
        return SUCCESS_CODE;
    }


    int32_t Build_And_Install_PKG(const std::string &pkg_query)
    {
        std::cout << "Building package!\n";
        const std::filesystem::path package_path = INSTALL_PATH + pkg_query;

        if (!std::filesystem::exists(package_path)) {
            std::cerr << WARNING_COLOUR <<  "PKG directory does not exist: " << package_path << "\n";
            return ERR_CODE;
        }

        std::filesystem::current_path(package_path);
        if (std::system("makepkg -risc")) {
            std::cerr << WARNING_COLOUR <<  "Failed to build package!\n";
            return ERR_CODE;
        }

        std::cout << "Successfully build package!\nCleaning directory...\n";
        Clean(pkg_query);

        std::filesystem::current_path(HOME_DIR);
        return SUCCESS_CODE;
    }

    // * Clean the package directory
    void Clean(const std::string &pkg_query)
    {
        std::filesystem::current_path(INSTALL_PATH);
        std::filesystem::remove_all(pkg_query);
    }


    std::vector<std::string> Check_For_Updates()
    {
        std::vector<std::string> pkgs_to_update;
        std::vector<std::string> pkg_list = Get_PKG_List();

        if (pkg_list.empty()) return pkgs_to_update;

        for (const auto &pkg : pkg_list) {
            std::regex end_with_debug(".*-debug$");
            std::istringstream iss(pkg);
            std::string pkg_version;
            std::string pkg_name;
            iss >> pkg_name >> pkg_version;

            if (std::regex_match(pkg_name, end_with_debug)) continue;

            std::string current_version = Get_PKG_Version(pkg_name);
            if (current_version == "Not found") {
                std::cerr << WARNING_COLOUR << "PKG " << pkg_name << " not found in the AUR!\n" << RESET;
                continue;
            }

            if (current_version != pkg_version) pkgs_to_update.push_back(pkg_name);
        }

        return pkgs_to_update;
    }


    int32_t Update_PKGs(const std::vector<std::string> packages_to_update, const bool &no_syu)
    {
        // ? Perform system update to avoid depedencies mismatch
        if (!no_syu) {
            if (std::system("sudo pacman -Syu")) {
                std::cerr << "System update failed, please do pacman -Syu manually!\n";
                return ERR_CODE;
            }
        }

        // ? Update AUR packages
        std::vector<std::string> updated_packages;
        for (const auto &pkg_name : packages_to_update) {
            std::cout << "Updating AUR packages!\n";
            if (!Install_AUR_PKG(pkg_name)) {
                std::cerr << "Failed to update package: " << pkg_name << '\n';
                return ERR_CODE;
            }
            updated_packages.push_back(pkg_name);
        }

        std::cout << "Successfully updated: ";
        for (const auto &pkg_name : updated_packages) std::cout << NAME_COLOUR << pkg_name << ' ';
        std::cout << '\n';
        return SUCCESS_CODE;
    }


    int32_t Install_AUR_PKG(const std::string &pkg_query)
    {
        if (!Check_For_Updates().empty()) std::cout << WARNING_COLOUR << "WARNING: " << RESET << "You have updates due!\n";
        if (Clone_AUR_PKG(pkg_query)) return ERR_CODE;
        if (Build_And_Install_PKG(pkg_query)) return ERR_CODE;
        return SUCCESS_CODE;
    }


    int32_t Remove_Installed_PKG(const std::string &pkg_query)
    {
        if (!Is_PKG_Installed(pkg_query)) {
            std::cerr << WARNING_COLOUR << "Package " << pkg_query << " not installed." << RESET << '\n';
        }

        const std::string command = "sudo pacman -Rns " + pkg_query;
        if (system(command.c_str())) return 1;
        return 0;
    }
};


int32_t main(int32_t argc, char **argv)
{
    CLI::App app{"AUR Helper Only"};

    std::string install_query;
    std::string remove_query;
    std::string search_query;
    bool only_name = false;
    bool is_list = false;
    bool no_syu = false;
    bool update = false;

    app.add_option("-S,--Sync", install_query, "Download packages");
    app.add_option("-s,--search", search_query, "Search for packages");
    app.add_flag("-n,--name", only_name, "Only list pkg's names. Use only with the --search option");
    app.add_flag("-U,--update", update, "Upgrade AUR packages, aswell upgrades the system");
    app.add_flag("--no-sysupgrade", no_syu, "Prevents the code to run pacman -Syu");
    app.add_flag("-Q,--query", is_list, "List installed AUR packages");
    app.add_option("-R,--Remove", remove_query, "Removes a package");

    CLI11_PARSE(app, argc, argv);

    AUR_Helper Hone;
    return Hone.Start(install_query, remove_query, search_query, only_name, is_list, update, no_syu);
}