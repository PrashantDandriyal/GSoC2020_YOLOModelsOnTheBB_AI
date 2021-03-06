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
// This source code is based on the "one_eo_per_frame" demo example provided in the TIDL API examples. 
// For more info, refer: https://git.ti.com/cgit/tidl/tidl-api/plain/examples/one_eo_per_frame/main.cpp
// It follows the Approach 1 of the project, as explained in the "README.md"
//
#include <signal.h>
#include <iostream>
#include <fstream>
#include <cassert>
#include <string>

#include "executor.h"
#include "execution_object.h"
#include "configuration.h"
#include "utils.h"

using namespace tidl;
using std::string;
using std::unique_ptr;
using std::vector;

bool Run(const string& config_file, int num_eve,int num_dsp,
         const char* ref_output);

Executor* CreateExecutor(DeviceType dt, int num, const Configuration& c);
void  CollectEOs(const Executor *e, vector<ExecutionObject *>& EOs);
void AllocateMemory(const vector<ExecutionObject *>& eos)

int main(int argc, char *argv[])
{
    // Catch ctrl-c to ensure a clean exit
    signal(SIGABRT, exit);
    signal(SIGTERM, exit);

    // If there are no devices capable of offloading TIDL on the SoC, exit
    uint32_t num_eve = Executor::GetNumDevices(DeviceType::EVE);
    uint32_t num_dsp = Executor::GetNumDevices(DeviceType::DSP);
    if (num_eve == 0 && num_dsp == 0)
    {
        std::cout << "TI DL not supported on this SoC." << std::endl;
        return EXIT_SUCCESS;
    }

    string config_file ="../cfg/app_1.txt";
    string ref_file    ="../models/yolov2tiny_ref.bin";

    unique_ptr<const char> reference_output(ReadReferenceOutput(ref_file));

    // Enable time stamp generation. The timestamp file is post processed
    // by execution_graph.py to generate graphical view of frame execution.
    // Refer to the User's Guide for details.
    EnableTimeStamps("1eo.log");

    bool status = Run(config_file, num_eve, num_dsp, reference_output.get());

    if (!status)
    {
        std::cout << "FAILED" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "PASSED" << std::endl;
    return EXIT_SUCCESS;
}

bool Run(const string& config_file, int num_eve, int num_dsp,
         const char* ref_output)
{
    Configuration c;
    if (!c.ReadFromFile(config_file))
        return false;

    /// Display heap usage statistics
    c.showHeapStats = true;

    // Heap sizes for this network determined using Configuration::showHeapStats
    c.PARAM_HEAP_SIZE   = (18 << 20); // 2 Executor x 9MB/Executor
    c.NETWORK_HEAP_SIZE = (384 << 20); // 6 EO x 64MB/EO

    //Process first 30
    c.numFrames = 30;

    // Open input file for reading
    std::ifstream input_data_file(c.inData, std::ios::binary);

    bool status = true;
    try
    {
        // Create Executors - use all the DSP and EVE cores available
        unique_ptr<Executor> e_dsp(CreateExecutor(DeviceType::DSP, num_dsp, c));
        unique_ptr<Executor> e_eve(CreateExecutor(DeviceType::EVE, num_eve, c));

        // Accumulate all the EOs from across the Executors
        vector<ExecutionObject *> EOs;
        CollectEOs(e_eve.get(), EOs);
        CollectEOs(e_dsp.get(), EOs);

        AllocateMemory(EOs);

        // Process frames with EOs in a pipelined manner
        // additional num_eos iterations to flush the pipeline (epilogue)
        int num_eos = EOs.size();
        for (int frame_idx = 0; frame_idx < c.numFrames + num_eos; frame_idx++)
        {
            ExecutionObject* eo = EOs[frame_idx % num_eos];

            // Wait for previous frame on the SAME! eo to finish processing
            if (eo->ProcessFrameWait())
            {
                //"ReportTime()" is used to report host and device execution times.
                ReportTime(eo);

                // Check for any possible error
                if (frame_idx < num_eos && !CheckFrame(eo, ref_output))
                    status = false;
            }
            
            // For first and new frames
            // Read a frame and start processing it with current eo
            if (ReadFrame(eo, frame_idx, c, input_data_file))
                eo->ProcessFrameStartAsync();
        }

        FreeMemory(EOs);

    }
    catch (tidl::Exception &e)
    {
        std::cerr << e.what() << std::endl;
        status = false;
    }

    input_data_file.close();

    return status;
}

// Create an Executor with the specified type and number of EOs
Executor* CreateExecutor(DeviceType dt, int num, const Configuration& c)
{
    if (num == 0) 
        return nullptr;

    DeviceIds ids;
    for (int i = 0; i < num; i++)
        ids.insert(static_cast<DeviceId>(i));

    return new Executor(dt, ids, c);
}

// Accumulate EOs from an Executor into a vector of EOs
void CollectEOs(const Executor *e, vector<ExecutionObject *>& EOs)
{
    if (!e) return;

    for (unsigned int i = 0; i < e->GetNumExecutionObjects(); i++)
        EOs.push_back((*e)[i]);
}

// Allocate Memory
void AllocateMemory(const vector<ExecutionObject *>& eos)
{
    // Allocate input and output buffers for each execution object
    for (auto eo : eos)
    {
        size_t in_size  = eo->GetInputBufferSizeInBytes();
        size_t out_size = eo->GetOutputBufferSizeInBytes();
        void*  in_ptr   = malloc(in_size);
        void*  out_ptr  = malloc(out_size);
        assert(in_ptr != nullptr && out_ptr != nullptr);

        ArgInfo in  = { ArgInfo(in_ptr,  in_size)};
        ArgInfo out = { ArgInfo(out_ptr, out_size)};
        eo->SetInputOutputBuffer(in, out);
    }
}