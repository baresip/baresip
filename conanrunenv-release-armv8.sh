script_folder="/Users/palmarti/development/baresip"
echo "echo Restoring environment" > "$script_folder/deactivate_conanrunenv-release-armv8.sh"
for v in GSTREAMER_ROOT GST_PLUGIN_SCANNER OPENSSL_MODULES
do
    is_defined="true"
    value=$(printenv $v) || is_defined="" || true
    if [ -n "$value" ] || [ -n "$is_defined" ]
    then
        echo export "$v='$value'" >> "$script_folder/deactivate_conanrunenv-release-armv8.sh"
    else
        echo unset $v >> "$script_folder/deactivate_conanrunenv-release-armv8.sh"
    fi
done


export GSTREAMER_ROOT="/Users/palmarti/.conan2/p/b/gstrebff9f943b2271/p"
export GST_PLUGIN_SCANNER="/Users/palmarti/.conan2/p/b/gstrebff9f943b2271/p/bin/gstreamer-1.0/gst-plugin-scanner"
export OPENSSL_MODULES="/Users/palmarti/.conan2/p/b/opens08d9f5d52652c/p/lib/ossl-modules"