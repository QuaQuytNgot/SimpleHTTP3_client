#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#define VIDEO_CHUNK_LEN 10000 //ms
#define REBUF_PENALTY 2.5 
double VIDEO_BIT_RATE[] = {500, 1000, 1500}; //kbps

double harmonic_mean(double arr[], int len){
  double ans = 0;
  for(int i = 0; i < len; i++){
    ans += 1/arr[i];
  }
  double final_ans = len/ans;
  return final_ans;
}

int mpc(double past_bandwidth[], int past_bandwidth_len, double past_bandwidth_ests[], int past_bandwidth_ests_len, double past_errors[], int past_errors_len, int P, double all_future_chunks_size[][P], 
double buffer_size, int chunk_sum, int video_chunk_remain, int last_quality){
  int total_combos = pow(3, P);
  int CHUNK_COMBO_OPTIONS[total_combos][P];
  for(int i = 0; i < total_combos; i++){
    int temp = i;
    for(int j = 0; j < P; j++){
      CHUNK_COMBO_OPTIONS[i][j] = temp % 3;
      temp /= 3;
    }
  }
  //tao CHUNK_COMBO_OPTIONS va tao chunk combinatiuon options

  double copy_past_bandwidth_ests[10000];
  for(int i = 0; i < past_bandwidth_ests_len; i++){
    copy_past_bandwidth_ests[i] = past_bandwidth_ests[i];
  }

  double copy_past_errors[10000];
  for(int i = 0; i < past_errors_len; i++){
    copy_past_errors[i] = past_errors[i];
  }
  //past_bw_ests va past_errors khong the duoc thay doi

  double curr_error = 0;  //gia su ban dau bang 0 vi chua thuc hien tinh toan lan nao
  if(past_bandwidth_ests_len > 0){
    curr_error = fabs(copy_past_bandwidth_ests[past_bandwidth_ests_len - 1] - past_bandwidth[past_bandwidth_len - 1])/(past_bandwidth[past_bandwidth_len - 1]);
  }
  copy_past_errors[past_errors_len] = curr_error;
  past_errors_len++;
  
  int start_id = past_bandwidth_len - 5 < 0 ? 0 : past_bandwidth_len - 5;
  double past_bandwidths[5];
  for(int i = 0; i < 5 && (start_id + i) < past_bandwidth_len; i++){
    past_bandwidths[i] = past_bandwidth[start_id + i];
  }
  // int start_id = past_bandwidth_len - 5;
  // double past_bandwidths[10000];
  // for(int i = 0; i < 5; i++){
  //   past_bandwidths[i] = past_bandwidth[start_id + i];
  // }

  //loai bo phan tu bang 0 o dau mang
  while (past_bandwidths[0] == 0 && past_bandwidth_len > 0) {
      // dich chuyen cac ptu ve phia truoc 1 don vi
      for (int i = 0; i < past_bandwidth_len - 1; i++) {
          past_bandwidths[i] = past_bandwidths[i + 1];
      }
      past_bandwidth_len--; // Giam do dai mang
  }

  double bandwidth_sum = 0;
  for(int i = 0; i < 5; i++){
    bandwidth_sum += 1/past_bandwidths[i];
  }
  double harmonic_bandwidth = 1/(bandwidth_sum/5); //5 la do dai cua past_bandwidths
//tinh harmonic_bw

  int error_pos = -5;
  double max_error = 0;
  if(past_errors_len < 5){
    error_pos = -past_errors_len;
  }
  error_pos = past_errors_len + error_pos;
  //chuyen doi am sang duong
  for(int i = error_pos; i < past_errors_len; i++){
    if(max_error < copy_past_errors[i]){
      max_error = copy_past_errors[i];
    }
    //max_error = max(copy_past_errors[i], max_error);
  }
  //tim gia tri lon nhat trong khoang tu error_pos den cuoi mang
  double future_bandwidth = harmonic_bandwidth/(1.0 + max_error);
  copy_past_bandwidth_ests[past_bandwidth_ests_len] = harmonic_bandwidth;
  //them harmonic_bandwidth vao cuoi mang copy_past_bandwidth_ests
  double start_buffer = buffer_size;
  int best_combo[5]; //10
  double max_reward = -100000.00;

  for(int i = 0; i < total_combos; i++){
    double curr_rebuffer_time = 0;
    double curr_buffer = start_buffer; //ms
    double bitrate_sum = 0;
    double smoothess_diffs = 0;

    for(int position = 0; position < P; position++){
      int chunk_quality = CHUNK_COMBO_OPTIONS[i][position];
      double download_time = 1000 * (all_future_chunks_size[chunk_quality][position] / 1000000.0) / (future_bandwidth / 1000.0); //sang giay // chia future_bw cho 1000 de Mbps truoc khi tinh toan download time

      if(curr_buffer < download_time){
        curr_rebuffer_time += (download_time - curr_buffer);
        curr_buffer = 0;
      }
      else{
        curr_buffer -= download_time;
      }
      curr_buffer += VIDEO_CHUNK_LEN;
      bitrate_sum += VIDEO_BIT_RATE[chunk_quality]; //kbps
      smoothess_diffs += fabs(VIDEO_BIT_RATE[chunk_quality] - VIDEO_BIT_RATE[last_quality]);
      last_quality = chunk_quality;
    }

    double reward = (bitrate_sum/1000.0) - (REBUF_PENALTY * curr_rebuffer_time / 1000.0) - (smoothess_diffs / 1000.0);  //mbps

    if(reward >= max_reward){
      if(best_combo[0] != 0 && best_combo[0] < CHUNK_COMBO_OPTIONS[i][0]){
        for(int k = 0; k < P; k++){
          best_combo[k] = CHUNK_COMBO_OPTIONS[i][k];
        }  
      } 
      else{
        for(int k = 0; k < P; k++){
          best_combo[k] = CHUNK_COMBO_OPTIONS[i][k];
        }
      }
      max_reward = reward;
    }
  }
  int bit_rate = best_combo[0];
  return bit_rate;  
}

size_t write_callback(void *data, size_t size, size_t nmemb, void *client){
  FILE *file = (FILE *)client;  
  size_t write = fwrite(data, size, nmemb, file);
  return write;
}

int main(int argc, char *argv[])
{
  CURL *hnd;
  hnd = curl_easy_init();
  int count = 1; 
  double past_bandwidth[10000];
  double past_bandwidth_ests[10000];
  double past_errors[10000];
  int est_index = 0;
  int index = 0;
  int err_index = 0;
  clock_t start = clock();//s
  int P = 5;
  double all_future_chunks_size[3][P];
  
  while(1){
    char filename[20];
    snprintf(filename, sizeof(filename), "output%d.mp4", count);
    FILE *segment = fopen(filename, "wb");
    double interval = (double)(clock() - start)/CLOCKS_PER_SEC * 1000;//s to ms
    if(interval > 1.0){
      CURLcode res;
      curl_easy_setopt(hnd, CURLOPT_URL, "https://127.0.0.1:8000/192.168.101.245:8000/input.mp4");  
      curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
      curl_easy_setopt(hnd, CURLOPT_USERAGENT, "CurlRequest");
      curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1l);
      curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
      curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_callback);
      curl_easy_setopt(hnd, CURLOPT_WRITEDATA, segment);
      curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_3ONLY);
      curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);
      res = curl_easy_perform(hnd);   
      if(res != CURLE_OK){
        fprintf(stderr, "Segment %d has errors: %s\n", count, curl_easy_strerror(res));
      }
      else{
        printf("Segment %d downloaded succesfully.\n", count);
        //tim past_bandwidth
        double speed;
        res = curl_easy_getinfo(hnd, CURLINFO_SPEED_DOWNLOAD, &speed);
        past_bandwidth[index] = speed;
        ++index;
        //past_bandwidth_ests su dung harmonic mean cho 5 gai tri dau cua past_bandwidth, khong co thi gan bang speed
        if(index >= 5){
          past_bandwidth_ests[est_index] = harmonic_mean(&past_bandwidth[index - 5], 5);
        }
        else{
          past_bandwidth_ests[est_index] = speed;
        }
        ++est_index;
        //tinh error giua gia tri thuc te va gia tri ests
        if(est_index > 0){
          past_errors[err_index] = fabs(past_bandwidth_ests[est_index - 1] - past_bandwidth[index - 1])/past_bandwidth[index - 1];
          ++err_index;
        }

        int bit_rate = mpc(past_bandwidth, index, past_bandwidth_ests, est_index, past_errors, err_index, P, all_future_chunks_size, 2000, 10, 10 - count, 1);
        //gia su last_quality ban dau la o giua {0, 1, 2}
        printf("Bitrate lựa chọn là: %d kbps\n", (int)VIDEO_BIT_RATE[bit_rate]);
      }
      count++;  
      start = (double)clock()/CLOCKS_PER_SEC;
      fclose(segment); 
    }
    if(count > 7)  break;
  }
   
  curl_easy_cleanup(hnd);
  hnd = NULL;
}


