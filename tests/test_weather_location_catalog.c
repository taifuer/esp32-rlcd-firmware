#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "weather_location_catalog.h"

static void assert_province(weather_province_id_t id,
                            const char *expected_name,
                            size_t expected_city_count)
{
    const weather_province_t *province =
        weather_location_province_by_id(id);
    assert(province != NULL);
    assert(province->id == id);
    assert(strcmp(province->name_zh, expected_name) == 0);
    assert(weather_location_city_count(id) == expected_city_count);
}

static void assert_city(weather_province_id_t province_id,
                        weather_city_id_t city_id,
                        const char *expected_name)
{
    const weather_city_t *city = weather_location_city_by_id(city_id);
    assert(city != NULL);
    assert(city->id == city_id);
    assert(city->province_id == province_id);
    assert(strcmp(city->name_zh, expected_name) == 0);
    assert(weather_location_city_belongs_to(province_id, city_id));
    assert(weather_location_selection_is_valid(province_id, city_id));
}

int main(void)
{
    assert(weather_location_province_count() == 31U);

    size_t total_cities = 0U;
    weather_province_id_t previous_province_id = 0U;
    weather_city_id_t previous_city_id = 0U;
    for (size_t province_index = 0U;
         province_index < weather_location_province_count();
         ++province_index) {
        const weather_province_t *province =
            weather_location_province_at(province_index);
        assert(province != NULL);
        assert(province->id > previous_province_id);
        assert(province->name_zh != NULL);
        assert(province->name_zh[0] != '\0');
        assert(weather_location_province_by_id(province->id) == province);
        previous_province_id = province->id;

        const size_t city_count = weather_location_city_count(province->id);
        assert(city_count > 0U);
        for (size_t city_index = 0U; city_index < city_count;
             ++city_index) {
            const weather_city_t *city = weather_location_city_at(
                province->id, city_index);
            assert(city != NULL);
            assert(city->id > previous_city_id);
            assert(city->province_id == province->id);
            assert(city->name_zh != NULL);
            assert(city->name_zh[0] != '\0');
            assert(weather_location_city_by_id(city->id) == city);
            assert(weather_location_city_belongs_to(province->id,
                                                     city->id));
            previous_city_id = city->id;
        }
        assert(weather_location_city_at(province->id, city_count) == NULL);
        total_cities += city_count;
    }
    assert(total_cities == WEATHER_LOCATION_CITY_COUNT);
    assert(weather_location_province_at(WEATHER_LOCATION_PROVINCE_COUNT) ==
           NULL);

    /* Directly administered municipality. */
    assert_province(110000U, "北京市", 1U);
    assert_city(110000U, 110000U, "北京市");

    /* Ordinary province and prefecture-level city. */
    assert_province(320000U, "江苏省", 13U);
    assert_city(320000U, 320100U, "南京市");

    /* Autonomous region and its capital. */
    assert_province(150000U, "内蒙古自治区", 12U);
    assert_city(150000U, 150100U, "呼和浩特市");

    /* Province-administered cities and counties keep their own AD codes. */
    assert_province(420000U, "湖北省", 17U);
    assert_city(420000U, 429004U, "仙桃市");
    assert_province(460000U, "海南省", 19U);
    assert_city(460000U, 469024U, "临高县");
    assert_province(650000U, "新疆维吾尔自治区", 26U);
    assert_city(650000U, 659001U, "石河子市");

    /* IDs cannot be mixed across parents or silently accepted. */
    assert(!weather_location_city_belongs_to(320000U, 110000U));
    assert(!weather_location_selection_is_valid(110000U, 320100U));
    assert(!weather_location_selection_is_valid(0U, 0U));
    assert(weather_location_province_by_id(0U) == NULL);
    assert(weather_location_province_by_id(999999U) == NULL);
    assert(weather_location_city_by_id(0U) == NULL);
    assert(weather_location_city_by_id(999999U) == NULL);
    assert(weather_location_city_count(999999U) == 0U);
    assert(weather_location_city_at(999999U, 0U) == NULL);

    puts("weather location catalog tests passed");
    return 0;
}
