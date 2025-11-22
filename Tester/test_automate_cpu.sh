exec_path="../Client/load_gen"
function=$1
log_file="./logs/"${function}"_clients_"
current_datetime=$(date +"%Y-%m-%d_%H-%M-%S")

taskset -c 4-7 "${exec_path}" 4 300 "${function}" >> "${log_file}${current_datetime}.log" &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 4 clients\n" >> "${log_file}${current_datetime}.log" 

taskset -c 4-7 "${exec_path}" 8 300 "${function}" >> "${log_file}${current_datetime}.log" &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 8 clients\n" >> "${log_file}${current_datetime}.log"

taskset -c 4-7 "${exec_path}" 16 300 "${function}" >> "${log_file}${current_datetime}.log" &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 16 clients\n" >> "${log_file}${current_datetime}.log"

taskset -c 4-7 "${exec_path}" 32 300 "${function}" >> "${log_file}${current_datetime}.log" &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 32 clients\n" >> "${log_file}${current_datetime}.log"

taskset -c 4-7 "${exec_path}" 64 300 "${function}" >> "${log_file}${current_datetime}.log"  &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 64 clients\n" >> "${log_file}${current_datetime}.log"

taskset -c 4-7 "${exec_path}" 128 300 "${function}" >> "${log_file}${current_datetime}.log"  &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 128 clients\n" >> "${log_file}${current_datetime}.log"

taskset -c 4-7 "${exec_path}" 256 300 "${function}" >> "${log_file}${current_datetime}.log" &
load_pid=$!
mpstat -P ALL 30 10 >> "${log_file}${current_datetime}.log" &
mpstat_pid=$!
wait $load_pid
wait $mpstat_pid
echo -e "\n    Completed ${function} with 256 clients\n" >> "${log_file}${current_datetime}.log"