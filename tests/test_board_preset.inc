// =====================================================================
// test_board_preset.inc — SD con presets + fábrica de fx + cadena en el
// pump (v0.7). Incluido VERBATIM dentro del namespace anónimo de
// test_all.cpp: usa check()/make_sine(); SIN #include, SIN main().
// =====================================================================

void test_sdcard_presets() {
    std::cout << "[test] sdcard v0.7: *.wav = canciones, *.json = presets\n";
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string dir = (fs::temp_directory_path() /
                             "deluge_sim_test_sd2").string();
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    { std::ofstream f(dir + "/zzz.wav"); f << 'x'; }
    { std::ofstream f(dir + "/aaa.wav"); f << 'x'; }
    { std::ofstream f(dir + "/mipreset.json"); f << "{\"version\":1}"; }
    { std::ofstream f(dir + "/aaa.json"); f << "{}"; }
    { std::ofstream f(dir + "/nota.txt"); f << 'x'; }
    deluge_sim::board::SdCard sd;
    std::string err;
    const bool ok = sd.mount(dir, err);
    check(ok, "montaje de carpeta SD");
    check(ok && sd.song_count() == 2 && sd.preset_count() == 2,
          "2 wav y 2 json contados por separado (el .txt no)");
    check(ok && sd.presets()[0].find("aaa.json") != std::string::npos &&
              sd.presets()[1].find("mipreset.json") != std::string::npos,
          "presets ordenados alfabéticamente");
    fs::remove_all(dir, ec);
}

void test_fx_factory() {
    std::cout << "[test] fx_factory: nombres estables y fx desconocido -> nullptr\n";
    const char* kNames[] = {"fet76", "opto", "eq", "plate", "tape"};
    const char* kWant[] = {"fet76_ref", "opto_comp_ref", "passive_eq",
                           "plate_reverb", "tape_sat"};
    bool all = true;
    for (int i = 0; i < 5; ++i) {
        auto p = deluge_sim::make_fx(kNames[i], {});
        all = all && p && std::string(p->name()) == kWant[i];
    }
    check(all, "los 5 fx se construyen con el nombre estable esperado");
    check(deluge_sim::make_fx("no_existe", {}) == nullptr,
          "fx desconocido -> nullptr");
    auto t = deluge_sim::make_fx("tape", {{"drive_db", 9.0}});
    check(t && std::string(t->name()) == "tape_sat",
          "mapa parcial aceptado (lo ausente toma default)");
}

void test_preset_chain_in_pump() {
    std::cout << "[test] pump con cadena construida desde un preset (factory)\n";
    using namespace deluge_sim;
    // Preset: tape -> eq (orden de inserción), con claves prefijadas como
    // las deja el board al cargar un JSON de la SD.
    ChainPreset pr;
    pr.version = 1;
    pr.chain = {"tape", "eq"};
    pr.params["tape"]["drive_db"] = 9.0;
    pr.params["eq"]["freq_hz"] = 1000.0;
    pr.params["eq"]["boost_db"] = 6.0;
    std::map<std::string, double> kv;
    for (const auto& [f, kvv] : pr.params)
        for (const auto& [k, v] : kvv) kv[f + "." + k] = v;

    deluge_sim::board::BoardClock c;
    deluge_sim::board::BoardPump pump(c);
    for (const std::string& fx : pr.chain) {
        auto p = make_fx(fx, effective_fx_params(kv, fx));
        check(p != nullptr, "factory construye '" + fx + "'");
        if (p) pump.add_processor(std::move(p));
    }
    check(pump.processor_count() == 2, "cadena de 2 eslabones desde preset");

    WavData song = make_sine(48000, 512, 0.5);
    std::string err;
    check(pump.load_song(song, err), "carga de canción con la cadena nueva");
    std::vector<float> out(128 * 2, 0.0f);
    pump.start();
    bool finite = true;
    size_t got = 0;
    for (;;) {
        const size_t n = pump.pump(out.data(), 128);
        if (n == 0) break;
        for (size_t i = 0; i < 2 * n; ++i)
            finite = finite && std::isfinite(out[i]);
        got += n;
    }
    check(got == 512 && finite, "512 marcos renderizados y finitos");

    pump.clear_processors();
    check(pump.processor_count() == 0, "clear_processors vacía la cadena");
}

