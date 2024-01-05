#!/bin/bash

target_device="/mnt/ent"

block_size="4K"

# List of workloads in MB           
workloads=("1" "5" "10" "50" "100" "200")

output_file="write_speed_test_target_400.txt"

num_iterations=30

for ((i = 1; i <= num_iterations; i++)); do
    echo "*************************************" >> "$output_file"
    echo "Starting iteration $i" >> "$output_file"

    echo "Starting iteration $i"
    echo "*************************************"

    for workload in "${workloads[@]}"; do
        # Calculate the count based on the workload size and block size
        count=$((workload * 1024 / 4))

        echo "Testing write speed for ${workload}MB workload..."
        
        echo -e "Workload: ${workload}MB\n" >> "$output_file"


        { time dd if=/dev/urandom of="${target_device}/random_file" bs="${block_size}" count="${count}" iflag=fullblock; } 2>> "$output_file"

        echo "------------------------------------------"

        echo "------------------------------------------" >> "$output_file"

        sudo rm "${target_device}/random_file"
    done
done













# #!/bin/bash

# # Specify the path to your device mapper target (replace /dev/mapper/ent_dev with your target)
# device_mapper_target="/dev/mapper/ent_dev"

# # Specify the number of iterations for each file size
# num_iterations=1

# # Output file for timing information
# output_file="write_speed_test_target.txt"

# # Array of file sizes in megabytes
# file_sizes=("0.1" "0.5" "1" "2" "5")

# # Function to generate a random file of a given size
# generate_random_file() {
#     local file=$1
#     local size=$2
#     dd if=/dev/urandom of="$file" bs="$size" count=1
# }

# # Function to measure write speed
# measure_write_speed() {
#     local file=$1
#     local size=$2

#     # Measure and record write speed
#     (time dd if="$file" of="$device_mapper_target" bs="$size"M) 2>> "$output_file"
# }



# # Create or truncate the output file
# echo "Write Speed Test Results" > "$output_file"
# echo "------------------------" >> "$output_file"

# # Run the test for each file size
# for size in "${file_sizes[@]}"; do
#     for ((i = 1; i <= num_iterations; i++)); do
#         echo "Running iteration $i for file size ${size}MB..."
        
#         # Generate a random test file with varying sizes
#         file_size="${size}"
#         test_file="test_file_${size}MB_$i.bin"
#         generate_random_file "$test_file" "$file_size"M

#         # Measure write speed and append results to the output file
#         measure_write_speed "$test_file" "$file_size"M

#         # Clean up the test file
#         rm "$test_file"
#     done
# done

# echo "Test completed. Results saved in $output_file."






# TEST_DIR="/mnt/ent/fiotest"
# sudo mkdir -p $TEST_DIR

# for ((i = 1; i <= num_iterations; i++)); do
#     echo "Running iteration $i"

    
#      sudo fio --name=write_iops --directory=$TEST_DIR --size=400M \
# --time_based --runtime=60s --ramp_time=2s --ioengine=libaio --direct=1 \
# --verify=0 --bs=4K --iodepth=256 --rw=randwrite --group_reporting=1  \
# --iodepth_batch_submit=256  --iodepth_batch_complete_max=256 --output="${output_file}_$i" --allow_mounted_write=1
    
#     # sudo rmdir $TEST_DIR

# done




# for ((i = 1; i <= num_iterations; i++)); do
#     echo "Running iteration $i"
        
#     # Running this command causes data loss on the second device.
#     # We strongly recommend using a throwaway VM and disk.
#     sudo fio --name=write_iops_test \
#   --filename="$device_mapper_target" --filesize=512M \
#   --time_based --ramp_time=2s --runtime=1m \
#   --ioengine=libaio --direct=1 --verify=0 --randrepeat=0 \
#   --bs=4K --iodepth=256 --rw=randwrite \
#   --iodepth_batch_submit=256  --iodepth_batch_complete_max=256 --output="${output_file}_$i" --allow_mounted_write=1

# done