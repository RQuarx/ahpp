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
#include <array>
#include <regex>

using json = nlohmann::json;


class AUR_Helper
{
public:
    int32_t Start(const std::string &install_query, const std::string &search_query, const bool &only_name, const bool &is_list, const bool &update, const bool &no_syu)
    {
        // ? Restrict the use of multiple arguments
        if (Is_More_Than_One_Options(install_query, search_query, is_list, update)) {
            std::cerr << "Error: You can only use a single option at a time!\n";
            return 1;
        }
        // ? Checks if --name is being used when --search is not being used
        if (is_list && search_query.empty()) {
            std::cerr << "Error: Do not use --name outside of --search!\n";
            return 1;
        }
        // ? Checks if --no-sysupgrade is being used when --update is not being used
        if (no_syu && !update) {
            std::cerr << "Error: Do not use --no-sysupgrade outside of --update!\n";
            return 1;
        }

        if (!search_query.empty()) Search_Packages(search_query, only_name);
        else if (!install_query.empty()) Install_AUR_Package(install_query);
        else if (is_list) List_Package();
        else if (update) Perform_Upgrades(no_syu);
        return 0;
    }

private:
    const std::string HOME_DIR = std::getenv("HOME");
    const std::string INSTALL_PATH = HOME_DIR + "/.cache/hone/";
    const std::string PKG_LIST_PATH = INSTALL_PATH + "Installed/";


    bool Does_Install_Dir_Exists()
    {
        return std::filesystem::exists(INSTALL_PATH) && std::filesystem::is_directory(INSTALL_PATH);
    }


    bool Does_Package_List_Exists()
    {
        return std::filesystem::exists(PKG_LIST_PATH) && std::filesystem::is_directory(PKG_LIST_PATH);
    }


    bool Is_More_Than_One_Options(const std::string &install_query, const std::string &search_query, bool is_list, bool update)
    {
        int8_t option_count = 0;
        if (!install_query.empty()) option_count++;
        if (!search_query.empty()) option_count++;
        if (is_list) option_count++;
        if (update) option_count++;

        if (option_count > 1) {
            return 1;
        }

        return 0;
    }

    // ? Callback function to write response data from curl
    static std::size_t Write_Callback(void *contents, std::size_t size, std::size_t nmemb, std::string *userp)
    {
        userp->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }


    bool Is_Package_Installed(const std::string &pkg_name)
    {
        std::string command = "pacman -Q " + pkg_name + " 2>/dev/null";
        std::array<char, 128> buffer;
        std::string result;

        auto pipe = popen(command.c_str(), "r");
        if (!pipe) return false;

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        pclose(pipe);
        return !result.empty();
    }


    void Search_Packages(const std::string &search_query, bool only_name)
    {
        std::string read_buffer;
        CURLcode res;
        CURL *curl = curl_easy_init();;
        if (!curl) return;

        std::string url = "https://aur.archlinux.org/rpc/?v=5&type=search&arg=" + search_query; 
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        if (curl_easy_perform(curl) != CURLE_OK) {
            std::cerr << "Failed to perform curl request.\n";
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
                std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";
                std::string installed_text = Is_Package_Installed(pkg_name) ? "(Installed)" : "";

                std::cout << NAME_COLOUR + pkg_name << ' ' << VERSION_COLOUR + pkg_version << ' ' << INSTALLED_COLOUR + installed_text << '\n';
                std::cout << "    " << RESET + pkg_desc << '\n';
            }
        }
    }


    std::string Get_Package_Version(const std::string &pkg_name)
    {
        std::string read_buffer;
        CURL *curl = curl_easy_init();

        if (curl) {
            std::string url = "https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + pkg_name;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            auto json_response = json::parse(read_buffer);
            if (json_response["resultcount"] > 0) return json_response["results"][0]["Version"];
        }

        return "Not found";
    }


    bool Add_To_List_Install(const std::string &added_package)
    {
        std::string version = Get_Package_Version(added_package);
        std::ofstream list("package_list.txt", std::ios::app);

        if (!list) {
            std::cerr << "Failed to open file!\n";
            return false;
        }

        list << added_package << ' ' << version << '\n';
        list.close();
        return true;
    }


    bool Add_To_List_Update(std::vector<std::string> added_packages)
    {
        std::vector<std::string> packages;
        std::ifstream list_read(PKG_LIST_PATH + "package_list.txt");
        if (!list_read)  {
            std::cerr << "Failed to open package list file for reading!\n";
            return false;
        }

        std::string package;
        while (std::getline(list_read, package)) packages.push_back(package);
        list_read.close();

        for (const auto& added_pkg : added_packages) {
            bool found = false;
            for (const auto& pkg : packages) {
                std::istringstream iss(pkg);
                std::string pkg_name, pkg_version;
                iss >> pkg_name >> pkg_version;

                if (added_pkg == pkg_name) {
                    found = true; // ? Package already exists, no need to add
                    break;
                }
            }
            // ? If the package was not found, add it with its current version
            if (!found) packages.push_back(added_pkg + ' ' + Get_Package_Version(added_pkg));
        }

        std::ofstream list(PKG_LIST_PATH + "package_list.txt", std::ios::trunc);
        if (!list) {
            std::cerr << "Failed to open package list file for writing!\n";
            return false; 
        }
        for (const auto& pkg : packages) list << pkg << '\n';

        return true;
    }


    // * @param add if true, then the function will switch to add mode, if false, the function will only list package
    bool List_Package(const bool &add = false, const std::string &added_package = "", const bool &update = false)
    {
        if (!Does_Package_List_Exists()) {
            std::filesystem::create_directory(PKG_LIST_PATH);
            std::ofstream list(PKG_LIST_PATH + "package_list.txt");
            list.close();
        }

        if (add) return Add_To_List_Update(Check_For_Updates());
        if (update) return Add_To_List_Install(added_package);

        std::ifstream list("package_list.txt");
        if (!list) {
            std::cerr << "Failed to open file!\n";
            return false;
        }

        std::string package;
        while (std::getline(list, package)) std::cout << package << '\n';

        list.close();
        return true;
    }


    bool Perform_Upgrades(const bool &no_syu)
    {
        std::cout << "Performing upgrades!\n";
        if (!Update_Packages(Check_For_Updates(), no_syu)) return false;
        List_Package(false, "", true);
        return true;
    }


    bool Clone_AUR_Package(const std::string &package_query)
    {
        if (!Does_Install_Dir_Exists()) std::filesystem::create_directory(INSTALL_PATH);

        std::filesystem::current_path(INSTALL_PATH);
        std::string command = "git clone https://aur.archlinux.org/" + package_query + ".git " + INSTALL_PATH + package_query;
        bool result = !std::system(command.c_str());

        if (!result) std::cerr << "Failed to clone AUR package?\n";

        return result;
    }


    bool Build_And_Install_Package(const std::string &package_query)
    {
        std::filesystem::path package_path = INSTALL_PATH + package_query;

        if (!std::filesystem::exists(package_path)) {
            std::cerr << "Package directory does not exist: " << package_path << "\n";
            return false;
        }

        std::filesystem::current_path(package_path);
        bool result = !std::system("makepkg -risc");

        std::filesystem::current_path(HOME_DIR);

        if (!result) {
            std::cerr << "Failed to build package!\n";
            return result;
        }
        
        std::cout << "Successfully build package!\nCleaning directory...\n";
        Clean(package_query);

        return result;
    }

    // * Clean the package directory
    void Clean(const std::string &package_query)
    {
        std::filesystem::current_path(INSTALL_PATH);
        std::filesystem::remove_all(package_query);
    }


    std::vector<std::string> Check_For_Updates()
    {
        std::vector<std::string> packages_to_update;
        std::ifstream list(PKG_LIST_PATH + "package_list.txt");

        if (!list) {
            std::cerr << "Failed to open package list file!\n";
            return packages_to_update;
        }

        std::string package;
        while (std::getline(list, package)) {
            std::istringstream iss(package);
            std::string pkg_version;
            std::string pkg_name;
            iss >> pkg_name >> pkg_version;

            std::string current_version = Get_Package_Version(pkg_name);
            if (current_version == "Not found") {
                std::cerr << "Package " << pkg_name << " not found in the AUR!\n";
                continue;
            }

            if (current_version != pkg_version) packages_to_update.push_back(pkg_name);
        }

        list.close();
        return packages_to_update;
    }


    bool Update_Packages(const std::vector<std::string> packages_to_update, const bool &no_syu)
    {
        // ? Perform system update to avoid depedencies mismatch
        if (!no_syu) {
            bool system_update_result = !std::system("sudo pacman -Syu");

            if (!system_update_result) {
                std::cerr << "System update failed, please do pacman -Syu manually!\n";
                return false;
            }
        }

        // ? Update AUR packages
        std::vector<std::string> updated_packages;
        for (const auto &pkg_name : packages_to_update) {
            std::cout << "Updating AUR packages!\n";
            if (!Install_AUR_Package(pkg_name)) {
                std::cerr << "Failed to update package: " << pkg_name << '\n';
                return false;
            }
            updated_packages.push_back(pkg_name);
        }

        std::cout << "Successfully updated: ";
        for (const auto &pkg_name : updated_packages) std::cout << pkg_name << ' ';
        std::cout << '\n';
        return true;
    }


    bool Install_AUR_Package(const std::string &package_query)
    {
        if (!Check_For_Updates().empty()) std::cout << WARNING_COLOUR << "WARNING: " << RESET << "You have updates due!\n";

        std::cout << "Cloning package!\n";
        if (!Clone_AUR_Package(package_query)) return false;

        std::cout << "Building package!\n";
        if (!Build_And_Install_Package(package_query)) return false;
        
        List_Package(true, package_query);
        return true;
    }
};


int32_t main(int32_t argc, char **argv)
{
    CLI::App app{"AUR Helper Only"};

    std::string install_query;
    std::string search_query;
    bool only_name = false;
    bool is_list = false;
    bool no_syu = false;
    bool update = false;

    app.add_option("-d,--download", install_query, "Download packages");
    app.add_option("-s,--search", search_query, "Search for packages");
    app.add_flag("-n,--name", only_name, "Only list pkg's names. Use only with the --search option");
    app.add_flag("-l,--list", is_list, "List installed AUR packages");
    app.add_flag("-u,--update", update, "Upgrade AUR packages, aswell upgrades the system");
    app.add_flag("--no-sysupgrade", no_syu, "Prevents the code to run pacman -Syu");

    CLI11_PARSE(app, argc, argv);

    AUR_Helper Hone;
    return Hone.Start(install_query, search_query, only_name, is_list, update, no_syu);
}