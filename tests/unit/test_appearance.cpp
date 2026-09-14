#include "doctest.h"
#include "bhr/appearance.hpp"
#include "bhr/display_transform.hpp"
#include "bhr/disk_appearance.hpp"
#include "bhr/presets.hpp"
#include "bhr/image.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <limits>
#include <unistd.h>

TEST_CASE("static disk appearance is bounded periodic and fades only the outer edge") {
    constexpr float pi=3.14159265358979323846f;
    CHECK(bhr::disk_appearance(10,0,4,20,0,0)==1);
    CHECK(bhr::disk_appearance(20,0,4,20,.18f,.1f)==0);
    CHECK(bhr::disk_appearance(19.2f,0,4,20,0,.1f)==doctest::Approx(.5).epsilon(1e-5));
    CHECK(bhr::disk_appearance(4,0,4,20,0,.1f)==1);
    for(int i=0;i<100;++i) {
        const float r=4+.14f*i,phi=.13f*i;
        const float x=bhr::disk_appearance(r,phi,4,20,.18f,0);
        CHECK(x>=1.0f-.18f);CHECK(x<=1.0f+.18f);
        CHECK(x==doctest::Approx(bhr::disk_appearance(r,phi+2*pi,4,20,.18f,0)).epsilon(1e-5));
    }
}

TEST_CASE("cinematic display uses specified linear exposure and exactly one sRGB encoding") {
    CHECK(bhr::srgb_encode(0.0031308f) == doctest::Approx(0.040449936).epsilon(1e-6));
    CHECK(bhr::srgb_encode(0.18f) == doctest::Approx(0.461356130).epsilon(1e-6));
    CHECK(bhr::srgb_encode(0.5f) == doctest::Approx(0.735356983).epsilon(1e-6));
    CHECK(bhr::srgb_decode(0.04045f) == doctest::Approx(0.003130804954).epsilon(1e-6));
    CHECK(bhr::display_channel(1, 0) == 188);
    CHECK(bhr::display_channel(1, 1) == 213);
    CHECK(bhr::display_channel(2, 0) == bhr::display_channel(1, 1));
    CHECK(bhr::display_channel(0, 16) == 0);
    CHECK(bhr::display_channel(65504, 16) == 255);
    CHECK(bhr::srgb_encode(0) == 0);
    CHECK(bhr::srgb_encode(1) == doctest::Approx(1));
    CHECK(bhr::srgb_decode(1) == doctest::Approx(1));
    CHECK(bhr::valid_radiance({0,1,65504,1}));
    CHECK_FALSE(bhr::valid_radiance({-1,0,0,1}));
    CHECK_FALSE(bhr::valid_radiance({0,0,0,0}));
    CHECK_FALSE(bhr::valid_radiance({65505,0,0,1}));
    CHECK_FALSE(bhr::valid_radiance({std::numeric_limits<float>::quiet_NaN(),0,0,1}));
    CHECK_FALSE(bhr::valid_radiance({0,std::numeric_limits<float>::infinity(),0,1}));
}

TEST_CASE("cinematic v2 explicitly upgrades while preserving strict v1 and frame evaluation") {
    const auto old = bhr::load_cinematic("presets/shots/reference.json");
    const auto before = bhr::serialize_cinematic(old);
    const auto doc = bhr::upgrade_cinematic(old, {});
    const auto text = bhr::serialize_cinematic_v2(doc);
    CHECK_THROWS(bhr::parse_cinematic(text));
    CHECK_THROWS(bhr::parse_cinematic_v2(before));
    CHECK(bhr::serialize_cinematic_v2(bhr::parse_cinematic_v2(text)) == text);
    CHECK(bhr::serialize_cinematic(old) == before);
    CHECK(std::holds_alternative<bhr::CinematicDocument>(bhr::parse_render_document(before)));
    CHECK(std::holds_alternative<bhr::CinematicRenderDocument>(bhr::parse_render_document(text)));
    const auto frame = bhr::evaluate_frame(doc.scene_shots, "orbit", 300);
    CHECK(frame.params.camera.r_cam == bhr::evaluate_frame(old,"orbit",300).params.camera.r_cam);
    CHECK(frame.time.numerator == 300);
    CHECK(bhr::serialize_cinematic(doc.scene_shots) == before);
    const auto sizes = bhr::cinematic_sizes(1920,1080);
    CHECK(sizes.radiance_bytes == 33177600);
    CHECK(sizes.bloom_width == 480);
    CHECK(sizes.bloom_height == 270);
    CHECK(bhr::cinematic_sizes(1,1).bloom_bytes == 16);
    CHECK(bhr::cinematic_sizes(7,5).bloom_height == 2);
    CHECK_THROWS(bhr::cinematic_sizes(0,1));
    CHECK_THROWS(bhr::cinematic_sizes(-1,1));
    CHECK_THROWS(bhr::cinematic_sizes(16384,16384));
    const auto params=bhr::workbench_preset();
    CHECK_NOTHROW(bhr::validate_cinematic_request({params,{}}));
    auto bad_params=params;bad_params.spin=-1;
    CHECK_THROWS(bhr::validate_cinematic_request({bad_params,{}}));
    CHECK_THROWS(bhr::validate_cinematic_request({params,{0}}));
    bad_params=params;bad_params.camera.width=16384;bad_params.camera.height=16384;
    CHECK_THROWS(bhr::validate_cinematic_request({bad_params,{}}));
}

TEST_CASE("appearance rejects malformed values transactionally and retains strict structure") {
    using Json=nlohmann::json;
    const auto doc=bhr::upgrade_cinematic(bhr::import_preset(bhr::workbench_preset()),{});
    const auto text=bhr::serialize_cinematic_v2(doc);
    const auto root=Json::parse(text);
    const auto invalid=[&](const std::string& pointer, const Json& value) {
        auto next=root; next[Json::json_pointer(pointer)]=value;
        CHECK_THROWS(bhr::parse_render_document(next.dump()));
    };
    for (const auto key : {"temperature_scale","disk_detail","outer_fade_fraction","star_intensity","exposure_ev"}) {
        for(const auto& value : {Json("1"),Json(nullptr),Json(true),Json(-100),Json(1e100),Json(1e-100)})
            invalid(std::string("/appearance/")+key,value);
    }
    for(const auto key : {"working_space","radiance_model","tone_map","output_transfer"}) {
        invalid(std::string("/appearance/")+key,"unknown");
        invalid(std::string("/appearance/")+key,42);
    }
    for(const auto& value : {Json(2),Json(1.0),Json(-1),Json("1")}) invalid("/appearance/schema_version",value);
    invalid("/appearance/bloom/enabled",1);
    invalid("/appearance/bloom/strength",.2);
    invalid("/appearance/bloom/threshold",0);
    invalid("/appearance/extra",true);
    invalid("/appearance/bloom/extra",true);
    invalid("/schema_version",3);
    invalid("/extra",true);
    invalid("/scene/extra",true);
    invalid("/scene/preset/schema_version",2);
    for(const auto& item:root["appearance"].items()) {
        auto missing=root;missing["appearance"].erase(item.key());
        CHECK_THROWS(bhr::parse_cinematic_v2(missing.dump()));
    }
    CHECK_THROWS(bhr::parse_render_document(text+std::string("\0x",2)));
    CHECK_THROWS(bhr::parse_render_document(text+" true"));
    CHECK_THROWS(bhr::parse_render_document("{\"appearance\":{\"exposure_ev\":1,\"exposure_ev\":2}}"));
    CHECK_THROWS(bhr::parse_render_document(std::string(1024*1024+1,' ')));
    CHECK_THROWS(bhr::parse_render_document("{\"a\":[[[[[[[[]]]]]]]]}"));
    CHECK(bhr::serialize_cinematic_v2(doc)==text);
    CHECK_THROWS(bhr::validate_appearance({std::numeric_limits<float>::quiet_NaN()}));
    CHECK_NOTHROW(bhr::validate_appearance({.01f,0,0,0,-16,{false,0,.1f}}));
    CHECK_NOTHROW(bhr::validate_appearance({4,.3f,.25f,16,16,{true,.15f,16}}));
}

TEST_CASE("cinematic v2 saves atomically and loads without changing source snapshots") {
    std::string pattern="/tmp/bhr-appearance-XXXXXX";
    const auto directory=mkdtemp(pattern.data()); REQUIRE(directory);
    const std::filesystem::path dir(directory), path=dir/"scene.json";
    const auto doc=bhr::upgrade_cinematic(bhr::import_preset(bhr::workbench_preset()),{});
    const auto text=bhr::serialize_cinematic_v2(doc);
    bhr::save_cinematic_v2(doc,path);
    CHECK(bhr::serialize_cinematic_v2(bhr::load_cinematic_v2(path))==text);
    CHECK(std::holds_alternative<bhr::CinematicRenderDocument>(bhr::load_render_document(path)));
    CHECK_THROWS(bhr::save_cinematic_v2({doc.scene_shots,{0}},path));
    CHECK(bhr::serialize_cinematic_v2(bhr::load_cinematic_v2(path))==text);
    CHECK_THROWS(bhr::save_cinematic_v2(doc,dir));
    CHECK_THROWS(bhr::save_cinematic_v2(doc,dir/"absent"/"scene.json"));
    CHECK_THROWS(bhr::save_cinematic_v2(doc,{}));
    CHECK_THROWS(bhr::load_render_document(dir/"absent"));
    CHECK_THROWS(bhr::load_render_document(dir));
    CHECK_THROWS(bhr::load_render_document({}));
    std::filesystem::remove_all(dir);
}

TEST_CASE("cinematic PNG declares sRGB and preserves destination on invalid input") {
    const std::string path="/tmp/bhr-color-patch-"+std::to_string(getpid())+".png";
    bhr::Image image; image.allocate(1,1); image.set_pixel(0,0,188,0,0);
    REQUIRE(bhr::write_srgb_png(image,path));
    const auto read=[&] { std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{}); };
    const auto original=read();
    REQUIRE(original.size()>46);
    CHECK(original.substr(37,4)=="sRGB");
    CHECK(static_cast<unsigned char>(original[41])==0);
    // Independently known PNG sRGB/intent-0 CRC: AE CE 1C E9.
    CHECK(original.substr(42,4)==std::string("\xae\xce\x1c\xe9",4));
    CHECK_FALSE(bhr::write_srgb_png({},path));
    CHECK(read()==original);
    CHECK_FALSE(bhr::write_srgb_png(image,"/absent-bhr-directory/image.png"));
    CHECK_FALSE(bhr::write_srgb_png(image,{}));
    CHECK_FALSE(bhr::write_srgb_png({1,1,{1}},path));
    std::filesystem::remove(path);
}
