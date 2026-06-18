/*
Purpose: RuntimeConfig — mutable settings backed by ESP32 NVS (Preferences).
Compile-time defaults come from UserConfiguration.h and TimingConfiguration.h.
*/
#include "config/RuntimeConfig.h"
#include "config/UserConfiguration.h"
#include "config/TimingConfiguration.h"
#include "config/APIConfiguration.h"
#include <Preferences.h>

// NVS namespace (≤15 chars)
static constexpr const char *kNS = "fw_cfg";

// Global instance — default-initialised to zeros; loadConfig() fills it.
RuntimeConfig g_config;

// Write all compile-time defaults into c.
static void applyDefaults(RuntimeConfig &c)
{
    c.center_lat                    = UserConfiguration::CENTER_LAT;
    c.center_lon                    = UserConfiguration::CENTER_LON;
    c.radius_km                     = UserConfiguration::RADIUS_KM;
    c.display_brightness            = UserConfiguration::DISPLAY_BRIGHTNESS;
    c.text_color_r                  = UserConfiguration::TEXT_COLOR_R;
    c.text_color_g                  = UserConfiguration::TEXT_COLOR_G;
    c.text_color_b                  = UserConfiguration::TEXT_COLOR_B;
    c.display_nearest_only          = UserConfiguration::DISPLAY_NEAREST_ONLY;
    c.display_border                = UserConfiguration::DISPLAY_BORDER;
    c.show_flight_number_with_logo  = UserConfiguration::SHOW_FLIGHT_NUMBER_WITH_LOGO;
    c.show_aircraft_type            = UserConfiguration::SHOW_AIRCRAFT_TYPE;
    c.show_aircraft_registration    = UserConfiguration::SHOW_AIRCRAFT_REGISTRATION;
    c.swap_aircraft_type_reg        = UserConfiguration::SWAP_AIRCRAFT_TYPE_REG;
    strncpy(c.screen_facing, UserConfiguration::SCREEN_FACING,
            sizeof(c.screen_facing) - 1);
    c.screen_facing[sizeof(c.screen_facing) - 1] = '\0';
    c.display_flip               = false;
    c.night_mode_enabled         = false;
    c.night_start_minutes        = 22 * 60;   // 22:00
    c.night_end_minutes          =  7 * 60;   // 07:00
    c.night_brightness           = 0;
    c.utc_offset_minutes         = 0;
    // Local ADS-B — seed from compile-time constant (empty = OpenSky only)
    strncpy(c.tar1090_host, APIConfiguration::TAR1090_HOST, sizeof(c.tar1090_host) - 1);
    c.tar1090_host[sizeof(c.tar1090_host) - 1] = '\0';

    // API keys default to compile-time constants; NVS can override these later.
    strncpy(c.opensky_client_id,     APIConfiguration::OPENSKY_CLIENT_ID,     sizeof(c.opensky_client_id)     - 1);
    c.opensky_client_id[sizeof(c.opensky_client_id) - 1] = '\0';
    strncpy(c.opensky_client_secret, APIConfiguration::OPENSKY_CLIENT_SECRET, sizeof(c.opensky_client_secret) - 1);
    c.opensky_client_secret[sizeof(c.opensky_client_secret) - 1] = '\0';
    strncpy(c.aeroapi_key,           APIConfiguration::AEROAPI_KEY,           sizeof(c.aeroapi_key)           - 1);
    c.aeroapi_key[sizeof(c.aeroapi_key) - 1] = '\0';

    c.opensky_priority               = false;

    c.fetch_interval_seconds        = TimingConfiguration::FETCH_INTERVAL_SECONDS;
    c.local_fetch_interval_seconds  = TimingConfiguration::LOCAL_FETCH_INTERVAL_SECONDS;
    c.display_cycle_seconds         = TimingConfiguration::DISPLAY_CYCLE_SECONDS;
    c.aeroapi_cache_ttl_seconds     = TimingConfiguration::AEROAPI_CACHE_TTL_SECONDS;
    c.aeroapi_fail_cache_ttl_seconds = TimingConfiguration::AEROAPI_FAIL_CACHE_TTL_SECONDS;
}

void loadConfig()
{
    applyDefaults(g_config);          // start with compile-time defaults

    Preferences p;
    if (!p.begin(kNS, /*readOnly=*/false))
        return;                       // NVS not available — keep defaults

    if (!p.isKey("lat"))              // namespace exists but no saved config yet
    { p.end(); return; }

    g_config.center_lat   = p.getDouble("lat",   g_config.center_lat);
    g_config.center_lon   = p.getDouble("lon",   g_config.center_lon);
    g_config.radius_km    = p.getDouble("radius", g_config.radius_km);

    g_config.display_brightness = (uint8_t)p.getUInt("bright",  g_config.display_brightness);
    g_config.text_color_r       = (uint8_t)p.getUInt("tc_r",    g_config.text_color_r);
    g_config.text_color_g       = (uint8_t)p.getUInt("tc_g",    g_config.text_color_g);
    g_config.text_color_b       = (uint8_t)p.getUInt("tc_b",    g_config.text_color_b);

    g_config.display_nearest_only         = p.getBool("nearest",   g_config.display_nearest_only);
    g_config.display_border               = p.getBool("border",    g_config.display_border);
    g_config.show_flight_number_with_logo = p.getBool("flt_num",   g_config.show_flight_number_with_logo);
    g_config.show_aircraft_type           = p.getBool("show_type", g_config.show_aircraft_type);
    g_config.show_aircraft_registration   = p.getBool("show_reg",  g_config.show_aircraft_registration);
    g_config.swap_aircraft_type_reg       = p.getBool("swap_tr",   g_config.swap_aircraft_type_reg);

    String sf = p.getString("facing", String(g_config.screen_facing));
    strncpy(g_config.screen_facing, sf.c_str(), sizeof(g_config.screen_facing) - 1);
    g_config.screen_facing[sizeof(g_config.screen_facing) - 1] = '\0';

    g_config.display_flip           = p.getBool("flip",     g_config.display_flip);
    g_config.night_mode_enabled     = p.getBool("night_en", g_config.night_mode_enabled);
    g_config.night_start_minutes    = (uint16_t)p.getUInt("night_s",  g_config.night_start_minutes);
    g_config.night_end_minutes      = (uint16_t)p.getUInt("night_e",  g_config.night_end_minutes);
    g_config.night_brightness       = (uint8_t)p.getUInt("night_br",  g_config.night_brightness);
    g_config.utc_offset_minutes     = p.getInt("utc_off",  g_config.utc_offset_minutes);

    g_config.opensky_priority              = p.getBool("osky_pri",  g_config.opensky_priority);

    g_config.fetch_interval_seconds        = p.getUInt("fetch_iv",   g_config.fetch_interval_seconds);
    g_config.local_fetch_interval_seconds  = p.getUInt("local_iv",   g_config.local_fetch_interval_seconds);
    g_config.display_cycle_seconds         = p.getUInt("disp_cyc",   g_config.display_cycle_seconds);
    g_config.aeroapi_cache_ttl_seconds     = p.getUInt("api_ttl",   g_config.aeroapi_cache_ttl_seconds);
    g_config.aeroapi_fail_cache_ttl_seconds = p.getUInt("api_fttl", g_config.aeroapi_fail_cache_ttl_seconds);

    {
        String v = p.getString("adsb_host", String(g_config.tar1090_host));
        strncpy(g_config.tar1090_host, v.c_str(), sizeof(g_config.tar1090_host) - 1);
        g_config.tar1090_host[sizeof(g_config.tar1090_host) - 1] = '\0';
    }
    {
        String v = p.getString("osky_id",  String(g_config.opensky_client_id));
        strncpy(g_config.opensky_client_id, v.c_str(), sizeof(g_config.opensky_client_id) - 1);
        g_config.opensky_client_id[sizeof(g_config.opensky_client_id) - 1] = '\0';
    }
    {
        String v = p.getString("osky_sec", String(g_config.opensky_client_secret));
        strncpy(g_config.opensky_client_secret, v.c_str(), sizeof(g_config.opensky_client_secret) - 1);
        g_config.opensky_client_secret[sizeof(g_config.opensky_client_secret) - 1] = '\0';
    }
    {
        String v = p.getString("aero_key", String(g_config.aeroapi_key));
        strncpy(g_config.aeroapi_key, v.c_str(), sizeof(g_config.aeroapi_key) - 1);
        g_config.aeroapi_key[sizeof(g_config.aeroapi_key) - 1] = '\0';
    }

    p.end();
}

void saveConfig()
{
    Preferences p;
    if (!p.begin(kNS, /*readOnly=*/false))
        return;

    p.putDouble("lat",    g_config.center_lat);
    p.putDouble("lon",    g_config.center_lon);
    p.putDouble("radius", g_config.radius_km);

    p.putUInt("bright",  g_config.display_brightness);
    p.putUInt("tc_r",    g_config.text_color_r);
    p.putUInt("tc_g",    g_config.text_color_g);
    p.putUInt("tc_b",    g_config.text_color_b);

    p.putBool("nearest",   g_config.display_nearest_only);
    p.putBool("border",    g_config.display_border);
    p.putBool("flt_num",   g_config.show_flight_number_with_logo);
    p.putBool("show_type", g_config.show_aircraft_type);
    p.putBool("show_reg",  g_config.show_aircraft_registration);
    p.putBool("swap_tr",   g_config.swap_aircraft_type_reg);

    p.putString("facing", String(g_config.screen_facing));

    p.putBool("flip",     g_config.display_flip);
    p.putBool("night_en", g_config.night_mode_enabled);
    p.putUInt("night_s",  g_config.night_start_minutes);
    p.putUInt("night_e",  g_config.night_end_minutes);
    p.putUInt("night_br", g_config.night_brightness);
    p.putInt("utc_off",   g_config.utc_offset_minutes);

    p.putBool("osky_pri",  g_config.opensky_priority);

    p.putUInt("fetch_iv",  g_config.fetch_interval_seconds);
    p.putUInt("local_iv",  g_config.local_fetch_interval_seconds);
    p.putUInt("disp_cyc",  g_config.display_cycle_seconds);
    p.putUInt("api_ttl",   g_config.aeroapi_cache_ttl_seconds);
    p.putUInt("api_fttl",  g_config.aeroapi_fail_cache_ttl_seconds);

    p.putString("adsb_host", g_config.tar1090_host);
    p.putString("osky_id",  g_config.opensky_client_id);
    p.putString("osky_sec", g_config.opensky_client_secret);
    p.putString("aero_key", g_config.aeroapi_key);

    p.end();
}

void resetConfig()
{
    applyDefaults(g_config);
    saveConfig();
}

bool isNightActive()
{
    if (!g_config.night_mode_enabled)
        return false;

    time_t utcNow = time(nullptr);
    if (utcNow < 946684800L)          // pre-2000 → NTP not yet synced
        return false;

    time_t localNow = utcNow + (time_t)((int32_t)g_config.utc_offset_minutes * 60);
    struct tm tmBuf;
    if (!gmtime_r(&localNow, &tmBuf))
        return false;

    int nowMin   = tmBuf.tm_hour * 60 + tmBuf.tm_min;
    int startMin = (int)g_config.night_start_minutes;
    int endMin   = (int)g_config.night_end_minutes;

    if (startMin <= endMin)
        return (nowMin >= startMin && nowMin < endMin);  // same-day window
    else
        return (nowMin >= startMin || nowMin < endMin);  // overnight span
}
