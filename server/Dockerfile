FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server.py .
COPY web ./web

# The TIDAL login is kept here so it survives restarts and updates.
ENV TIDAL_SESSION_FILE=/data/tidal-session.json
VOLUME /data

EXPOSE 8686
CMD ["python", "-u", "server.py"]
