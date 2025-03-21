import paho.mqtt.client as mqtt
import zmq
import json
import time

class CounterReceiver:
    def __init__(self, endpoint):
        # Create a ZeroMQ context
        self.context = zmq.Context()
        
        #Create a PULL socket to receive messages
        self.socket = self.context.socket(zmq.PULL)
        self.socket.bind(endpoint)  # Bind to the given endpoint

        self.BROKER = "localhost"  # Change if using a remote broker
        self.PORT = 1883
        self.TOPIC = "sabertooth/readout"
        self.client = mqtt.Client()
        self.client.connect(self.BROKER, self.PORT, 60)

    def receive_map(self):
        message = self.socket.recv().decode("utf-8")  # Receive and decode JSON string
        # received_map = json.loads(message)  # Convert JSON to Python dictionary
        print("Received map:", message)
        return message

    def receive_and_send_metrics(self):
        print("Connection opened")

        while True:
            # Sending data to Grafana
            # Example data to send to Grafana (can be JSON format or whatever Grafana expects)
            time.sleep(1)
            metrics = receiver.receive_map()
            # metrics =  {'adc_words': 0, 'dma_buffer_size': 300000, 'is_running': True, 'num_dma_loops': 6374, 'num_events': 12999, 'num_files': 13}
            #self.client.publish(self.TOPIC, json.dumps(metrics))
            self.client.publish(self.TOPIC, metrics)
            print("Published metrics..")


if __name__ == "__main__":
    # Create a CounterReceiver instance to listen on the same endpoint
    receiver = CounterReceiver("tcp://*:5555")
    print("Opened ZMQ and MQTT socket...")
    receiver.receive_and_send_metrics()


