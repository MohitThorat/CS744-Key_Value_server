import pandas as pd
import matplotlib.pyplot as plt
file_name = 'Threads_vs_ResponseTime.csv'
data_source = './data/put_all/' + file_name
# Read CSV (adjust filename if needed)
df = pd.read_csv(data_source)

# Extract columns
threads = df.iloc[:, 0]
throughput = df.iloc[:, 1]

# Plot
plt.figure(figsize=(8, 5))
plt.plot(threads, throughput, marker='o')
plt.xlabel("Number of Threads")
plt.ylabel("Response Time (us)")
plt.title("Threads vs Response Time")
plt.grid(True)
plt.tight_layout()

# Save to current directory
plt.savefig("./plots/" + file_name.replace('.csv', '.png'))

print("Saved plot .png")