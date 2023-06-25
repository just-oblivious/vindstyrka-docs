"""
This crude script logs the values received from sen54logger, along with VINDSTYRKA Zigbee readings
taken from ZHA.

Output format:
2023-06-25T15:24:41.362165;1455130,1347;PRO,8,202,232,248,255,4313,5344,780,32767,RAW,4,3194,6448,31860,65535,MYS,3,3194,6448,64479,ZHA,4,23.0,81.0,4900,2500 # noqa E501

timestamp;ticks,count;LABEL,length,value0,..valueN,LABEL,length,value0,..valueN

Labels:
PRO: SEN54 Processed values
RAW: SEN54 Raw values
MYS: SEN54 Mystery values
ZHA: VINDSTYRKA Zigbee values
"""

import yaml
import websockets
import asyncio
import json
import logging
import serial

from datetime import datetime


logging.basicConfig(level=logging.INFO, format='[{levelname:4.4s}] {message}', style='{')
logger = logging.getLogger()


class ZHAReader(object):

    def __init__(self, uri, token):
        self.uri = uri
        self.token = token

    async def ha_send(self, ws, event_type, data):
        """ Send event to HA websocket """
        await ws.send(json.dumps({'type': event_type, **data}))

    async def ha_recv(self, ws):
        """ Receive events from HA websocket """
        return json.loads(await ws.recv())

    async def ha_await(self, ws, message_id):
        """ Await a message with a specific ID """
        while True:
            message = await self.ha_recv(ws)
            if message['id'] == message_id:
                return message

    async def get_values(self, ieee):
        clusters = [
            0x042a,  # PM2.5
            0xfc7e,  # VOC index
            0x0405,  # Humidity
            0x0402,  # Temperature
        ]

        # Open websocket
        async with websockets.connect(self.uri) as ws:

            # Authorize with HA
            if (await self.ha_recv(ws))['type'] == 'auth_required':
                await self.ha_send(ws, 'auth', {'access_token': self.token})
                auth_response = await self.ha_recv(ws)
                if not auth_response['type'] == 'auth_ok':
                    raise Exception(f'Auth failed: {auth_response}')

            message_id = 0
            values = []

            # Instruct ZHA to read the VINDSTYRKA attributes
            for cluster in clusters:
                # Construct and send ZHA message
                message_id += 1
                event_data = {
                    'id': message_id,
                    'ieee': ieee,
                    'endpoint_id': 1,
                    'cluster_id': cluster,
                    'cluster_type': 'in',
                    'attribute': 0
                }
                await self.ha_send(ws, 'zha/devices/clusters/attributes/value', event_data)

                # Await and log the response
                try:
                    response = await asyncio.wait_for(self.ha_await(ws, message_id), 5)
                    if not response['success']:
                        logger.error(f'Response: {response}')
                        return

                    values.append(response['result'])

                except asyncio.TimeoutError:
                    logger.error('Response timed out')

            return values


async def main(config):
    zha = ZHAReader(config['uri'], config['token'])

    # Read incoming sen54logger data
    with serial.Serial(config['serial'], 230400) as ser:
        with open(config['output_file'], 'a') as log_file:
            while(True):
                if sen54log := ser.readline().strip():
                    sen54log = sen54log.decode().rstrip(',')
                    if sen54log[0] in ('!', '#'):
                        logger.warning(f'Data error: {sen54log}')
                        continue  # Informational or error message

                    ts = datetime.now().isoformat()

                    # Request values from ZHA
                    if zha_values := await zha.get_values(config['vindstyrka']):
                        zha_log = ','.join(zha_values)
                        log_line = f"{ts};{sen54log},ZHA,{len(zha_values)},{zha_log}"

                        # Write to log file
                        log_file.write(log_line)
                        log_file.write('\n')
                        log_file.flush()


if __name__ == "__main__":
    with open('config.yml') as cf:
        config = yaml.safe_load(cf)

    asyncio.run(main(config))
