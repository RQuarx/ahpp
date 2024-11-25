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


class hone
{
    public:
        int32_t Start(const std::string &install_query, const std::string &search_query, const bool &only_name, const bool &is_list, const bool &update, const bool &no_syu)
        {
            // ? Restrict the use of multiple arguments
            int8_t option_count = 0;
            if (!install_query.empty()) option_count++;
            if (!search_query.empty()) option_count++;
            if (is_list) option_count++;
            if (update) option_count++;

            if (option_count > 1)
            {
                std::cerr << "Error: You can only use a single option at a time!\n";
                return 1;
            }

            // ? Checks if --name is being used when --search is not being used
            if (is_list && search_query.empty())
            {
                std::cerr << "Error: Do not use --name outside of --search!\n";
                return 1;
            }

            // ? Checks if --no-sysupgrade is being used when --update is not being used
            if (no_syu && !update)
            {
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
        // ? Callback function to write response data from curl
        static std::size_t Write_Callback(void *contents, std::size_t size, std::size_t nmemb, std::string *userp)
        {
            std::size_t total_size = size * nmemb;
            userp->append(static_cast<char*>(contents), total_size);
            return total_size;
        }


        bool Is_Package_Installed(const std::string &pkg_name)
        {
            std::string command = "pacman -Q " + pkg_name + " 2>/dev/null";
            std::array<char, 128> buffer;
            std::string result;

            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);

            if (!pipe)
            {
                std::cerr << "Failed to know if packages are installed or not\n";
                return false;
            }

            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
            {
                result += buffer.data();
            }

            return !result.empty();
        }


        void Search_Packages(const std::string &search_query, const bool &only_name)
        {
            std::string read_buffer;
            CURLcode res;
            CURL *curl = curl_easy_init();;

            if (curl)
            {
                std::string url = "https://aur.archlinux.org/rpc/?v=5&type=search&arg=" + search_query; 
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write_Callback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
                res = curl_easy_perform(curl);
                curl_easy_cleanup(curl);

                if (res != CURLE_OK)
                {
                    std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << '\n';
                    return;
                }

                // ? Parse json response
                auto json_response = json::parse(read_buffer);
                if (json_response["resultcount"] > 0)
                {
                    std::regex include_pattern(".*" + std::regex_replace(search_query, std::regex(R"([.*+?^${}()|\[\]\\])"), R"(\\$&)") + ".*", std::regex_constants::icase);

                    if (only_name)
                    {
                        for (const auto &pkg : json_response["results"])
                        {
                            std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";
                            if (std::regex_match(pkg_name, include_pattern)) std::cout << pkg_name << '\n';
                        }
                        return;
                    }

                    for (const auto &pkg : json_response["results"])
                    {
                        std::string pkg_desc = pkg.contains("Description") ? pkg["Description"] : "No description available";
                        std::string pkg_version = pkg.contains("Version") ? pkg["Version"] : "Unknown";
                        std::string pkg_name = pkg.contains("Name") ? pkg["Name"] : "Unknown";
                        std::string installed_text = Is_Package_Installed(pkg_name) ? "(Installed)" : "";

                        if (std::regex_match(pkg_name, include_pattern))
                        {
                            std::cout << NAME_COLOUR + pkg_name << ' ' << VERSION_COLOUR + pkg_version << ' ' << INSTALLED_COLOUR + installed_text << '\n';
                            std::cout << "    " << RESET + pkg_desc << '\n';
                        }
                    }
                }
                else std::cout << "No packages found.\n";
            }
        }

        // ? Get the home directory
        std::string Home()
        {
            return std::getenv("HOME");
        }

        // ? Get the package installation path
        std::string Install_Path()
        {
            return Home() + "/.cache/hone/";
        }

        // ? Get the package list file path
        std::string List_Path()
        {
            return Install_Path() + "Installed/";
        }


        bool Does_Install_Dir_Exists()
        {
            return std::filesystem::exists(Install_Path()) && std::filesystem::is_directory(Install_Path());
        }


        bool Does_Package_List_Exists()
        {
            return std::filesystem::exists(List_Path()) && std::filesystem::is_directory(List_Path());
        }


        std::string Get_Package_Version(const std::string &pkg_name)
        {
            std::string read_buffer;
            CURL *curl = curl_easy_init();

            if (curl)
            {
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

            if (!list)
            {
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
            std::ifstream list_read(List_Path() + "package_list.txt");
            if (!list_read) 
            {
                std::cerr << "Failed to open package list file for reading!\n";
                return false;
            }

            std::string package;
            while (std::getline(list_read, package)) 
            {
                packages.push_back(package);
            }
            list_read.close();
            for (const auto& added_pkg : added_packages) 
            {
                bool found = false;
                for (const auto& pkg : packages) 
                {
                    std::istringstream iss(pkg);
                    std::string pkg_name, pkg_version;
                    iss >> pkg_name >> pkg_version;

                    if (added_pkg == pkg_name) 
                    {
                        found = true; // ? Package already exists, no need to add
                        break;
                    }
                }
                // ? If the package was not found, add it with its current version
                if (!found) packages.push_back(added_pkg + ' ' + Get_Package_Version(added_pkg));
            }

            std::ofstream list(List_Path() + "package_list.txt", std::ios::trunc);
            if (!list) 
            {
                std::cerr << "Failed to open package list file for writing!\n";
                return false; 
            }
            for (const auto& pkg : packages) list << pkg << '\n';

            return true;
        }

        /*
            * @param add if true, then the function will switch to add mode, if false, the function will only list package
        */
        bool List_Package(const bool &add = false, const std::string &added_package = "", const bool &update = false)
        {
            if (!Does_Package_List_Exists())
            {
                std::filesystem::create_directory(List_Path());
                std::ofstream list(List_Path() + "package_list.txt");
                list.close();
            }

            if (add) return Add_To_List_Update(Check_For_Updates());
            if (update) return Add_To_List_Install(added_package);

            std::ifstream list("package_list.txt");
            if (!list)
            {
                std::cerr << "Failed to open file!\n";
                return false;
            }

            std::string package;
            while (std::getline(list, package))
            {
                std::cout << package << '\n';
            }

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
            if (!Does_Install_Dir_Exists())
            {
                std::filesystem::create_directory(Install_Path());
            }

            std::filesystem::current_path(Install_Path());
            std::string command = "git clone https://aur.archlinux.org/" + package_query + ".git " + Install_Path() + package_query;
            bool result = !std::system(command.c_str());

            if (!result)
            {
                std::cerr << "Failed to clone AUR package?\n";
            }

            return result;
        }


        bool Build_And_Install_Package(const std::string &package_query)
        {
            std::filesystem::path package_path = Install_Path() + package_query;

            if (!std::filesystem::exists(package_path))
            {
                std::cerr << "Package directory does not exist: " << package_path << "\n";
                return false;
            }

            std::filesystem::current_path(package_path);
            bool result = !std::system("makepkg -risc");

            std::filesystem::current_path(Home());

            if (!result) 
            {
                std::cerr << "Failed to build package!\n";
                return result;
            }
            
            std::cout << "Successfully build package!\nCleaning directory...\n";
            Clean(package_query);

            return result;
        }

        // ? Clean the package directory
        void Clean(const std::string &package_query)
        {
            std::filesystem::current_path(Install_Path());
            std::filesystem::remove_all(package_query);
        }


        std::vector<std::string> Check_For_Updates()
        {
            std::vector<std::string> packages_to_update;
            std::ifstream list(List_Path() + "package_list.txt");

            if (!list)
            {
                std::cerr << "Failed to open package list file!\n";
                return packages_to_update;
            }

            std::string package;
            while (std::getline(list, package))
            {
                std::istringstream iss(package);
                std::string pkg_version;
                std::string pkg_name;
                iss >> pkg_name >> pkg_version;

                std::string current_version = Get_Package_Version(pkg_name);
                if (current_version == "Not found")
                {
                    std::cerr << "Package " << pkg_name << " not found in the AUR!\n";
                    continue;
                }

                if (current_version != pkg_version)
                    packages_to_update.push_back(pkg_name);
            }

            list.close();
            return packages_to_update;
        }


        bool Update_Packages(const std::vector<std::string> packages_to_update, const bool &no_syu)
        {
            // ? Perform system update to avoid depedencies mismatch
            if (!no_syu) 
            {
                bool system_update_result = !std::system("sudo pacman -Syu");

                if (!system_update_result)
                {
                    std::cerr << "System update failed, please do pacman -Syu manually!\n";
                    return false;
                }
            }

            // ? Update AUR packages
            std::vector<std::string> updated_packages;
            for (const auto &pkg_name : packages_to_update)
            {
                std::cout << "Updating AUR packages!\n";
                if (!Install_AUR_Package(pkg_name))
                {
                    std::cerr << "Failed to update package: " << pkg_name << '\n';
                    return false;
                }
                updated_packages.push_back(pkg_name);
            }

            std::cout << "Successfully updated: ";
            for (const auto &pkg_name : updated_packages)
            {
                std::cout << pkg_name << ' ';
            }
            std::cout << '\n';
            return true;
        }


        bool Install_AUR_Package(const std::string &package_query)
        {
            if (!Check_For_Updates().empty()) std::cout << "You have updates due!\n";

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

    hone Hone;
    return Hone.Start(install_query, search_query, only_name, is_list, update, no_syu);
}