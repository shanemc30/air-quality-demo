/*
 *  Copyright (c) 2017-2018, Sensirion AG
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Sensirion AG nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is based on the example usage for the SPS30 from it's library
 *  - https://github.com/Sensirion/embedded-sps
 *
 * as well as the Sensirion ESS library
 *  - https://github.com/winkj/wiced-ess
 */


#include "wiced.h"
#include "http_client.h"

#include "sensirion_ess.h"
#include "sps30.h"
#include "sensirion_common_wiced.h"

#define DELAY_TIME 500

// Use this define to enable data upload
#define ENABLE_DATA_UPLOAD

#include "http_send_message.h"

void application_start( )
{
    // ESS
    u16 tvoc_ppb, co2_eq_ppm;
    s32 temperature, humidity;

    // SPS30
    struct sps30_measurement m;
    u8 auto_clean_days = 4;
    u16 data_ready;
    s16 ret;

#ifdef ENABLE_DATA_UPLOAD
    int count = 0;
    wiced_result_t networkUp;
#endif /* ENABLE_DATA_UPLOAD */

    wiced_init();

    WPRINT_APP_INFO(("Air Quality Demo\n"));

#ifdef ENABLE_DATA_UPLOAD
    networkUp = init_http();
#endif /* ENABLE_DATA_UPLOAD */

    // adapt the following lines to your hardware configuration
    const ess_device_config_t device_config = {
            .i2c_port              = WICED_I2C_1,
            .needs_init_workaround = 0,
            .leds_supported        = 0,
            .flags                 = I2C_DEVICE_NO_DMA
    };

    // -- device probing
    while (ess_init(&device_config) != WICED_SUCCESS) {
        WPRINT_APP_INFO(("ESS probing failed\n"));
        wiced_rtos_delay_milliseconds(DELAY_TIME);
    }

    while (sps30_probe() != 0) {
        WPRINT_APP_INFO(("SPS sensor probing failed\n"));
        wiced_rtos_delay_milliseconds(DELAY_TIME);
    }
    WPRINT_APP_INFO(("SPS sensor probing successful\n"));

    // -- setup SPS30 sensor
    ret = sps30_set_fan_auto_cleaning_interval_days(auto_clean_days);
    if (ret)
        WPRINT_APP_INFO(("error %d setting the auto-clean interval\n", ret));

    ret = sps30_start_measurement();
    if (ret < 0)
        WPRINT_APP_INFO(("error starting measurement\n"));
    WPRINT_APP_INFO(("measurements started\n"));
    wiced_rtos_delay_milliseconds(DELAY_TIME);

    // -- main loop
        // -- read SGP30, connected to the ESS (environmental sensor shield)
        while (1) {
        wiced_result_t ret = ess_measure_iaq(&tvoc_ppb, &co2_eq_ppm);
        if (ret == WICED_SUCCESS) {
            WPRINT_APP_INFO(("tVOC %4u | CO2eq %4u | ", tvoc_ppb, co2_eq_ppm));
        } else {
            WPRINT_APP_INFO(("error reading IAQ values\n"));
        }

        // -- read SHTC1, connected to the ESS (environmental sensor shield)
        ret = ess_measure_rht(&temperature, &humidity);
        if (ret == WICED_SUCCESS) {
            WPRINT_APP_INFO(("T   %2.2f | RH   %2.2f\n", temperature / 1000.0f, humidity / 1000.0f));
        } else {
            WPRINT_APP_INFO(("error reading RH/T values\n"));
        }

        // -- check for data ready flag on SPS30
        ret = sps30_read_data_ready(&data_ready);
        if (ret < 0) {
            WPRINT_APP_INFO(("error %d reading data-ready flag\n", ret));
        }

        if (data_ready) {
            // -- read out SPS30
            ret = sps30_read_measurement(&m);
            if (ret < 0) {
                WPRINT_APP_INFO(("error reading measurement\n"));
            } else {
                 WPRINT_APP_INFO(("measured values:\n"
                         "\t%0.2f pm1.0\n"
                         "\t%0.2f pm2.5\n"
                         "\t%0.2f pm4.0\n"
                         "\t%0.2f pm10.0\n"
                         "\t%0.2f nc0.5\n"
                         "\t%0.2f nc1.0\n"
                         "\t%0.2f nc2.5\n"
                         "\t%0.2f nc4.5\n"
                         "\t%0.2f nc10.0\n"
                         "\t%0.2f typical particle size\n\n",
                         m.mc_1p0, m.mc_2p5, m.mc_4p0, m.mc_10p0,
                         m.nc_0p5, m.nc_1p0, m.nc_2p5, m.nc_4p0, m.nc_10p0,
                         m.typical_particle_size));
            }
       }

#ifdef ENABLE_DATA_UPLOAD
        // -- send data to cloud dashboard
        if (networkUp == WICED_SUCCESS) {
            if (count % 2 == 0) { // <-- rate limit
                send_message_http(temperature / 1000.0f, humidity / 1000.0f, tvoc_ppb, co2_eq_ppm,
                        m.mc_1p0, m.mc_2p5-m.mc_1p0, m.mc_4p0-m.mc_2p5, m.mc_10p0-m.mc_4p0);
            }
            count++;
        }
#endif /* ENABLE_DATA_UPLOAD */


        // Important: for accurate gas measurements on the SGP30, it is important
        // to maintain a 1 Hz readout cycle. To ensure this, the following things should be done:
        //   1. perform the http upload in a separate thread, to avoid going over the 1s
        //   2. measure the duration of the measurement, and only sleep for the delta of 1s-duration
        //   3. Potentially move application code unrelated to the SPS30 to another thread
        //
        // For the sake of simplicity, the author opted for a simple 1s delay here instead,
        // potentially sacrificing a little bit of accuracy
        wiced_rtos_delay_milliseconds(DELAY_TIME);
    }

}
