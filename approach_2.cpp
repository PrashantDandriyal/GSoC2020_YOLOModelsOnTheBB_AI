/******************************************************************************
 * Copyright (c) 2017-2018  Texas Instruments Incorporated - http://www.ti.com/
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Texas Instruments Incorporated nor the
 *        names of its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

//
// This source code is based on the "two_eo_per_frame" demo example provided in the TIDL API examples. 
// For more info, refer: https://git.ti.com/cgit/tidl/tidl-api/tree/examples/two_eo_per_frame_opt/main.cpp
// It follows the Approach 2 of the project, as explained in the "README.md"
//
#include <signal.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include <string>

#include "executor.h"
#include "execution_object.h"
#include "execution_object_pipeline.h"
#include "configuration.h"
#include "utils.h"

using namespace tidl;
using std::string;
using std::unique_ptr;
using std::vector;

using EOP = tidl::ExecutionObjectPipeline;

bool Run(int num_eve,int num_dsp, const char* ref_output);

int main(int argc, char *argv[])
{
    // Catch ctrl-c to ensure a clean exit
    signal(SIGABRT, exit);
    signal(SIGTERM, exit);

    // This example requires both EVE and C66x
    uint32_t num_eve = Executor::GetNumDevices(DeviceType::EVE);
    uint32_t num_dsp = Executor::GetNumDevices(DeviceType::DSP);
    if (num_eve == 0 || num_dsp == 0)
    {
        std::cout << "TIDL not supported on this SoC." << std::endl;
        return EXIT_SUCCESS;
    }

    //Use the same config file used for approach 1
    string config_file ="../cfg/app_1.txt";
    string ref_file    ="../models/yolov2tiny_ref.bin";
    unique_ptr<const char> reference_output(ReadReferenceOutput(ref_file));

    // Enable time stamp generation. The timestamp file is post processed
    // by execution_graph.py to generate graphical view of frame execution.
    // Refer to the User's Guide for details.
    EnableTimeStamps("2eo_opt.log");

    bool status = Run(num_eve, num_dsp, reference_output.get());

    if (!status)
    {
        std::cout << "FAILED" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "PASSED" << std::endl;
    return EXIT_SUCCESS;
}

bool Run(int num_eve, int num_dsp, const char* ref_output)
{
    Configuration c;
    if (!c.ReadFromFile(config_file))
        return false;

    /// Display heap usage statistics
    c.showHeapStats = true;

    // Heap sizes for this network determined using Configuration::showHeapStats
    c.PARAM_HEAP_SIZE   = (9 << 20); // 1 Executor x 9MB/Executor
    c.NETWORK_HEAP_SIZE = (256 << 20); // 4 EO x 64MB/EO

    // Run this example for 30frames
    c.numFrames = 30;

    // Assign layers 12, 13 and 14 to the DSP layer group
    const int EVE_LG = 1;
    ///const int DSP_LG = 2;
    ///c.layerIndex2LayerGroupId = { {12, DSP_LG}, {13, DSP_LG}, {14, DSP_LG} };

    /*
    The TIDL device translation tool assigns layer group ids to layers during the translation process.
    So, no explicit grouping is done here.
    */

    // Open input file for reading
    std::ifstream input(c.inData, std::ios::binary);

    bool status = true;
    try
    {
        // Create Executors - use all the DSP and EVE cores available
        // Specify layer group id for each Executor
        unique_ptr<Executor> eve(CreateExecutor(DeviceType::EVE,
                                                num_eve, c, EVE_LG));

        // On AM5749, create a total of 4 pipelines (EOPs):
        // EOPs[0] : { EVE1, EVE2 }
        // EOPs[1] : { EVE1, EVE2 } for double buffering
        // EOPs[2] : { EVE3, EVE4 }
        // EOPs[3] : { EVE3, EVE4 } for double buffering

        const uint32_t pipeline_depth = 2;  // 2 EOs in EOP => depth 2
        std::vector<EOP *> EOPs;
        uint32_t num_pipe = num_eve/2;  // = 2
        for (uint32_t i = 0; i < num_pipe; i=i+2)
            for (uint32_t j = 0; j < pipeline_depth; j++)
                EOPs.push_back(new EOP( { (*eve)[i % num_eve],
                                          (*eve)[(i+1) % num_eve] } ));

        AllocateMemory(EOPs);

        // Process frames with EOs in a pipelined manner
        // additional num_eos iterations to flush the pipeline (epilogue)
        int num_eops = EOPs.size();
        for (int frame_idx = 0; frame_idx < c.numFrames + num_eops; frame_idx++)
        {
            EOP* eop = EOPs[frame_idx % num_eops];

            // Wait for previous frame on the SAME! EOP to finish processing
            if (eop->ProcessFrameWait())
            {
                ///"ReportTime()" is used to report host and device execution times.
                ///ReportTime(eo);

                // Check for any possible error
                // The reference output is valid only for the first frame
                // processed on each EOP
                if (frame_idx < num_eops && !CheckFrame(eop, ref_output))
                    status = false;
            }

            // Read a frame and start processing it with current eo
            if (ReadFrame(eop, frame_idx, c, input))
                eop->ProcessFrameStartAsync();
        }

        FreeMemory(EOPs);

    }
    catch (tidl::Exception &e)
    {
        std::cerr << e.what() << std::endl;
        status = false;
    }

    input.close();

    return status;
}

void AllocateMemory(const vector<ExecutionObjectPipeline *>& eops)
{
    // Allocate input and output buffers for each execution object
    for (auto eop : eops)
    {
        size_t in_size  = eop->GetInputBufferSizeInBytes();
        size_t out_size = eop->GetOutputBufferSizeInBytes();
        void*  in_ptr   = malloc(in_size);
        void*  out_ptr  = malloc(out_size);
        assert(in_ptr != nullptr && out_ptr != nullptr);

        ArgInfo in  = { ArgInfo(in_ptr,  in_size)};
        ArgInfo out = { ArgInfo(out_ptr, out_size)};
        eop->SetInputOutputBuffer(in, out);
    }
}
