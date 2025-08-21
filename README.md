# navigator 🧭
Navigate between your HomeLab services

## preview
<img width="1774" height="985" alt="image" src="https://github.com/user-attachments/assets/e58bcbae-1867-4333-b386-e1728629a2ed" />
<img width="1777" height="993" alt="image" src="https://github.com/user-attachments/assets/d560cdaf-1d22-4dcd-9f86-6127ca7ae369" />

## credits
- @faustinoaq for porting the JS api to C

## features?
- super lightweight, in my testing it uses about ~2.2 mb for running the API and the site, the html site is only ~7kb so it fits in one packet
- dynamically adjusting config file, if you enter a entry in the service.json it'll automatically update the site without restart
- minimal and clean, meant to be as minimal as possible
- a status api checker built in that pings each site and all runs in one container
- dark mode/light mode button
- the entire container and setup is about ~56kb for everything

## how do i set this up?
1. git clone this repository
```
git clone https://github.com/rohanverma2007/navigator.git
```
2. cd into the navigator directory `cd navigator`

3. simply run the command `docker compose up -d` (or `podman-compose up -d` if you use podman)

4. should be running on port 11080, a reverse proxy is recommended for this setup

## reverse proxies?
I currently use Traefik on my podman containers, heres the labels I have on my navigator container, but you can really use whatever reverse proxy you want (etc. NGINX, Caddy, Apache)
```
    labels:
      - "traefik.enable=true"
      - "traefik.http.routers.navigate.rule=Host(`navigate.example.com`)"
      - "traefik.http.routers.navigate.entrypoints=websecure"
      - "traefik.http.routers.navigate.tls=true"
      - "traefik.http.routers.navigate.tls.certresolver=lets-encrypt"
      - "traefik.http.services.navigate.loadbalancer.server.port=11080"
```
